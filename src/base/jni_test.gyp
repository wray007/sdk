{
	'targets': [
		{
			'target_name': 'jni_headers',
		  'type': 'none',
		  'sources': [
		    'android/java/src/org/chromium/base/ActivityStatus.java',
        'android/java/src/org/chromium/base/BuildInfo.java',
        'android/java/src/org/chromium/base/CpuFeatures.java',
        'android/java/src/org/chromium/base/ImportantFileWriterAndroid.java',
        'android/java/src/org/chromium/base/MemoryPressureListener.java',
        'android/java/src/org/chromium/base/JavaHandlerThread.java',
        'android/java/src/org/chromium/base/PathService.java',
        'android/java/src/org/chromium/base/PathUtils.java',
        'android/java/src/org/chromium/base/PowerMonitor.java',
        'android/java/src/org/chromium/base/SystemMessageHandler.java',
        'android/java/src/org/chromium/base/SysUtils.java',
        'android/java/src/org/chromium/base/ThreadUtils.java',
		  ],
		  'variables': {
		    'jni_gen_package': 'base',
		    'optimize_jni_generation': 0
		  },
		  'includes': [ '../build/jni_generator.gypi' ],
		},
	],
}