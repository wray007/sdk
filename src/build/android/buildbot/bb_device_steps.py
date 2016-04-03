#!/usr/bin/env python
# Copyright (c) 2013 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import collections
import glob
import hashlib
import multiprocessing
import os
import random
import shutil
import sys

import bb_utils
import bb_annotations

sys.path.append(os.path.join(os.path.dirname(__file__), '..'))
import provision_devices
from pylib import android_commands
from pylib import constants
from pylib.gtest import gtest_config

sys.path.append(os.path.join(
    constants.DIR_SOURCE_ROOT, 'third_party', 'android_testrunner'))
import errors


CHROME_SRC = constants.DIR_SOURCE_ROOT
LOGCAT_DIR = os.path.join(CHROME_SRC, 'out', 'logcat')

# Describes an instrumation test suite:
#   test: Name of test we're running.
#   apk: apk to be installed.
#   apk_package: package for the apk to be installed.
#   test_apk: apk to run tests on.
#   test_data: data folder in format destination:source.
#   host_driven_root: The host-driven test root directory.
#   annotation: Annotation of the tests to include.
#   exclude_annotation: The annotation of the tests to exclude.
I_TEST = collections.namedtuple('InstrumentationTest', [
    'name', 'apk', 'apk_package', 'test_apk', 'test_data', 'host_driven_root',
    'annotation', 'exclude_annotation', 'extra_flags'])

def I(name, apk, apk_package, test_apk, test_data, host_driven_root=None,
      annotation=None, exclude_annotation=None, extra_flags=None):
  return I_TEST(name, apk, apk_package, test_apk, test_data, host_driven_root,
                annotation, exclude_annotation, extra_flags)

INSTRUMENTATION_TESTS = dict((suite.name, suite) for suite in [
    I('ContentShell',
      'ContentShell.apk',
      'org.chromium.content_shell_apk',
      'ContentShellTest',
      'content:content/test/data/android/device_files'),
    I('ChromiumTestShell',
      'ChromiumTestShell.apk',
      'org.chromium.chrome.testshell',
      'ChromiumTestShellTest',
      'chrome:chrome/test/data/android/device_files',
      constants.CHROMIUM_TEST_SHELL_HOST_DRIVEN_DIR),
    I('AndroidWebView',
      'AndroidWebView.apk',
      'org.chromium.android_webview.shell',
      'AndroidWebViewTest',
      'webview:android_webview/test/data/device_files'),
    ])

VALID_TESTS = set(['chromedriver', 'ui', 'unit', 'webkit', 'webkit_layout',
                   'webrtc'])

RunCmd = bb_utils.RunCmd


# multiprocessing map_async requires a top-level function for pickle library.
def RebootDeviceSafe(device):
  """Reboot a device, wait for it to start, and squelch timeout exceptions."""
  try:
    android_commands.AndroidCommands(device).Reboot(True)
  except errors.DeviceUnresponsiveError as e:
    return e


def RebootDevices():
  """Reboot all attached and online devices."""
  # Early return here to avoid presubmit dependence on adb,
  # which might not exist in this checkout.
  if bb_utils.TESTING:
    return
  devices = android_commands.GetAttachedDevices(emulator=False)
  print 'Rebooting: %s' % devices
  if devices:
    pool = multiprocessing.Pool(len(devices))
    results = pool.map_async(RebootDeviceSafe, devices).get(99999)

    for device, result in zip(devices, results):
      if result:
        print '%s failed to startup.' % device

    if any(results):
      bb_annotations.PrintWarning()
    else:
      print 'Reboots complete.'


def RunTestSuites(options, suites):
  """Manages an invocation of test_runner.py for gtests.

  Args:
    options: options object.
    suites: List of suite names to run.
  """
  args = ['--verbose']
  if options.target == 'Release':
    args.append('--release')
  if options.asan:
    args.append('--tool=asan')
  for suite in suites:
    bb_annotations.PrintNamedStep(suite)
    cmd = ['build/android/test_runner.py', 'gtest', '-s', suite] + args
    if suite == 'content_browsertests':
      cmd.append('--num_retries=1')
    RunCmd(cmd)

def RunChromeDriverTests(_):
  """Run all the steps for running chromedriver tests."""
  bb_annotations.PrintNamedStep('chromedriver_annotation')
  RunCmd(['chrome/test/chromedriver/run_buildbot_steps.py',
          '--android-packages=%s,%s,%s' %
           (constants.CHROMIUM_TEST_SHELL_PACKAGE,
            constants.CHROME_STABLE_PACKAGE,
            constants.CHROME_BETA_PACKAGE)])

def InstallApk(options, test, print_step=False):
  """Install an apk to all phones.

  Args:
    options: options object
    test: An I_TEST namedtuple
    print_step: Print a buildbot step
  """
  if print_step:
    bb_annotations.PrintNamedStep('install_%s' % test.name.lower())
  # TODO(gkanwar): Quick hack to make sure AndroidWebViewTest.apk is replaced
  # before AndroidWebView.apk is. This can be removed once the bots cycle.
  args = ['--apk', '%s.apk' % test.test_apk]
  if options.target == 'Release':
    args.append('--release')

  RunCmd(['build/android/adb_install_apk.py'] + args, halt_on_failure=True)

  args = ['--apk', test.apk, '--apk_package', test.apk_package]
  if options.target == 'Release':
    args.append('--release')

  RunCmd(['build/android/adb_install_apk.py'] + args, halt_on_failure=True)


def RunInstrumentationSuite(options, test, flunk_on_failure=True,
                            python_only=False, official_build=False):
  """Manages an invocation of test_runner.py for instrumentation tests.

  Args:
    options: options object
    test: An I_TEST namedtuple
    flunk_on_failure: Flunk the step if tests fail.
    Python: Run only host driven Python tests.
    official_build: Run official-build tests.
  """
  bb_annotations.PrintNamedStep('%s_instrumentation_tests' % test.name.lower())

  InstallApk(options, test)
  args = ['--test-apk', test.test_apk, '--test_data', test.test_data,
          '--verbose']
  if options.target == 'Release':
    args.append('--release')
  if options.asan:
    args.append('--tool=asan')
  if options.flakiness_server:
    args.append('--flakiness-dashboard-server=%s' %
                options.flakiness_server)
  if options.coverage_bucket:
    args.append('--coverage-dir=%s' % options.coverage_dir)
  if test.host_driven_root:
    args.append('--host-driven-root=%s' % test.host_driven_root)
  if test.annotation:
    args.extend(['-A', test.annotation])
  if test.exclude_annotation:
    args.extend(['-E', test.exclude_annotation])
  if test.extra_flags:
    args.extend(test.extra_flags)
  if python_only:
    args.append('-p')
  if official_build:
    # The option needs to be assigned 'True' as it does not have an action
    # associated with it.
    args.append('--official-build')

  RunCmd(['build/android/test_runner.py', 'instrumentation'] + args,
         flunk_on_failure=flunk_on_failure)


def RunWebkitLint(target):
  """Lint WebKit's TestExpectation files."""
  bb_annotations.PrintNamedStep('webkit_lint')
  RunCmd(['webkit/tools/layout_tests/run_webkit_tests.py',
          '--lint-test-files',
          '--chromium',
          '--target', target])


def RunWebkitLayoutTests(options):
  """Run layout tests on an actual device."""
  bb_annotations.PrintNamedStep('webkit_tests')
  cmd_args = [
        '--no-show-results',
        '--no-new-test-results',
        '--full-results-html',
        '--clobber-old-results',
        '--exit-after-n-failures', '5000',
        '--exit-after-n-crashes-or-timeouts', '100',
        '--debug-rwt-logging',
        '--results-directory', '..layout-test-results',
        '--target', options.target,
        '--builder-name', options.build_properties.get('buildername', ''),
        '--build-number', str(options.build_properties.get('buildnumber', '')),
        '--master-name', options.build_properties.get('mastername', ''),
        '--build-name', options.build_properties.get('buildername', ''),
        '--platform=android']

  for flag in 'test_results_server', 'driver_name', 'additional_drt_flag':
    if flag in options.factory_properties:
      cmd_args.extend(['--%s' % flag.replace('_', '-'),
                       options.factory_properties.get(flag)])

  for f in options.factory_properties.get('additional_expectations', []):
    cmd_args.extend(
        ['--additional-expectations=%s' % os.path.join(CHROME_SRC, *f)])

  # TODO(dpranke): Remove this block after
  # https://codereview.chromium.org/12927002/ lands.
  for f in options.factory_properties.get('additional_expectations_files', []):
    cmd_args.extend(
        ['--additional-expectations=%s' % os.path.join(CHROME_SRC, *f)])

  RunCmd(['webkit/tools/layout_tests/run_webkit_tests.py'] + cmd_args)


def SpawnLogcatMonitor():
  shutil.rmtree(LOGCAT_DIR, ignore_errors=True)
  bb_utils.SpawnCmd([
      os.path.join(CHROME_SRC, 'build', 'android', 'adb_logcat_monitor.py'),
      LOGCAT_DIR])

  # Wait for logcat_monitor to pull existing logcat
  RunCmd(['sleep', '5'])

def ProvisionDevices(options):
  bb_annotations.PrintNamedStep('provision_devices')

  if not bb_utils.TESTING:
    # Restart adb to work around bugs, sleep to wait for usb discovery.
    adb = android_commands.AndroidCommands()
    adb.RestartAdbServer()
    RunCmd(['sleep', '1'])

  if options.reboot:
    RebootDevices()
  provision_cmd = ['build/android/provision_devices.py', '-t', options.target]
  if options.auto_reconnect:
    provision_cmd.append('--auto-reconnect')
  RunCmd(provision_cmd)


def DeviceStatusCheck(_):
  bb_annotations.PrintNamedStep('device_status_check')
  RunCmd(['build/android/buildbot/bb_device_status_check.py'],
         halt_on_failure=True)


def GetDeviceSetupStepCmds():
  return [
    ('provision_devices', ProvisionDevices),
    ('device_status_check', DeviceStatusCheck),
  ]


def RunUnitTests(options):
  RunTestSuites(options, gtest_config.STABLE_TEST_SUITES)


def RunInstrumentationTests(options):
  for test in INSTRUMENTATION_TESTS.itervalues():
    RunInstrumentationSuite(options, test)


def RunWebkitTests(options):
  RunTestSuites(options, ['webkit_unit_tests'])
  RunWebkitLint(options.target)


def RunWebRTCTests(options):
  RunTestSuites(options, gtest_config.WEBRTC_TEST_SUITES)


def GetTestStepCmds():
  return [
      ('chromedriver', RunChromeDriverTests),
      ('unit', RunUnitTests),
      ('ui', RunInstrumentationTests),
      ('webkit', RunWebkitTests),
      ('webkit_layout', RunWebkitLayoutTests),
      ('webrtc', RunWebRTCTests),
  ]


def UploadCoverageData(options, path, coverage_type):
  """Uploads directory at |path| to Google Storage.

  The directory at path should ostensibly contain HTML coverage data.

  Args:
    options: Command line options.
    path: Path to the directory to be uploaded.
    coverage_type: String used as the first component of the url.

  Returns:
    None.
  """
  revision = options.build_properties.get('got_revision')
  if not revision:
    revision = options.build_properties.get('revision', 'testing')
  bot_id = options.build_properties.get('buildername', 'testing')
  randhash = hashlib.sha1(str(random.random())).hexdigest()
  gs_path = '%s/%s/%s/%s/%s' % (options.coverage_bucket, coverage_type,
                                bot_id, revision, randhash)

  RunCmd([bb_utils.GSUTIL_PATH, 'cp', '-R', path, 'gs://%s' % gs_path])
  bb_annotations.PrintLink(
      'Coverage report',
      'https://storage.googleapis.com/%s/index.html' % gs_path)


def GenerateJavaCoverageReport(options):
  """Generates an HTML coverage report using EMMA and uploads it."""
  bb_annotations.PrintNamedStep('java_coverage_report')

  coverage_html = os.path.join(options.coverage_dir, 'coverage_html')
  RunCmd(['build/android/generate_emma_html.py',
          '--coverage-dir', options.coverage_dir,
          '--metadata-dir', os.path.join(CHROME_SRC, 'out', options.target),
          '--cleanup',
          '--output', os.path.join(coverage_html, 'index.html')])
  UploadCoverageData(options, coverage_html, 'java')


def LogcatDump(options):
  # Print logcat, kill logcat monitor
  bb_annotations.PrintNamedStep('logcat_dump')
  logcat_file = os.path.join(CHROME_SRC, 'out', options.target, 'full_log')
  with open(logcat_file, 'w') as f:
    RunCmd([
        os.path.join(CHROME_SRC, 'build', 'android', 'adb_logcat_printer.py'),
        LOGCAT_DIR], stdout=f)
  RunCmd(['cat', logcat_file])


def GenerateTestReport(options):
  bb_annotations.PrintNamedStep('test_report')
  for report in glob.glob(
      os.path.join(CHROME_SRC, 'out', options.target, 'test_logs', '*.log')):
    RunCmd(['cat', report])
    os.remove(report)


def MainTestWrapper(options):
  try:
    # Spawn logcat monitor
    SpawnLogcatMonitor()

    # Run all device setup steps
    for _, cmd in GetDeviceSetupStepCmds():
      cmd(options)

    if options.install:
      test_obj = INSTRUMENTATION_TESTS[options.install]
      InstallApk(options, test_obj, print_step=True)

    if options.test_filter:
      bb_utils.RunSteps(options.test_filter, GetTestStepCmds(), options)

    if options.coverage_bucket:
      GenerateJavaCoverageReport(options)

    if options.experimental:
      RunTestSuites(options, gtest_config.EXPERIMENTAL_TEST_SUITES)

  finally:
    # Run all post test steps
    LogcatDump(options)
    GenerateTestReport(options)
    # KillHostHeartbeat() has logic to check if heartbeat process is running,
    # and kills only if it finds the process is running on the host.
    provision_devices.KillHostHeartbeat()


def GetDeviceStepsOptParser():
  parser = bb_utils.GetParser()
  parser.add_option('--experimental', action='store_true',
                    help='Run experiemental tests')
  parser.add_option('-f', '--test-filter', metavar='<filter>', default=[],
                    action='append',
                    help=('Run a test suite. Test suites: "%s"' %
                          '", "'.join(VALID_TESTS)))
  parser.add_option('--asan', action='store_true', help='Run tests with asan.')
  parser.add_option('--install', metavar='<apk name>',
                    help='Install an apk by name')
  parser.add_option('--reboot', action='store_true',
                    help='Reboot devices before running tests')
  parser.add_option('--coverage-bucket',
                    help=('Bucket name to store coverage results. Coverage is '
                          'only run if this is set.'))
  parser.add_option(
      '--flakiness-server',
      help='The flakiness dashboard server to which the results should be '
           'uploaded.')
  parser.add_option(
      '--auto-reconnect', action='store_true',
      help='Push script to device which restarts adbd on disconnections.')
  parser.add_option(
      '--logcat-dump-output',
      help='The logcat dump output will be "tee"-ed into this file')

  return parser


def main(argv):
  parser = GetDeviceStepsOptParser()
  options, args = parser.parse_args(argv[1:])

  if args:
    return sys.exit('Unused args %s' % args)

  unknown_tests = set(options.test_filter) - VALID_TESTS
  if unknown_tests:
    return sys.exit('Unknown tests %s' % list(unknown_tests))

  setattr(options, 'target', options.factory_properties.get('target', 'Debug'))
  if options.coverage_bucket:
    setattr(options, 'coverage_dir',
            os.path.join(CHROME_SRC, 'out', options.target, 'coverage'))

  MainTestWrapper(options)


if __name__ == '__main__':
  sys.exit(main(sys.argv))
