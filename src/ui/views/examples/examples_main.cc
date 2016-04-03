// Copyright 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/at_exit.h"
#include "base/command_line.h"
#include "base/files/file_path.h"
#include "base/run_loop.h"
#include "base/i18n/icu_util.h"
#include "base/message_loop/message_loop.h"
#include "ui/base/resource/resource_bundle.h"
#include "ui/base/ui_base_paths.h"
#include "ui/views/examples/examples_window.h"

#if defined(OS_WIN)
#include "ui/base/win/scoped_ole_initializer.h"
#endif

int main(int argc, char** argv) {
#if defined(OS_WIN)
  ui::ScopedOleInitializer ole_initializer_;
#endif

  CommandLine::Init(argc, argv);

  base::AtExitManager at_exit;

  base::MessageLoopForUI message_loop;

  base::i18n::InitializeICU();

  ui::RegisterPathProvider();
  ui::ResourceBundle::InitSharedInstanceWithLocale("en-US", NULL);

  views::examples::ShowExamplesWindow(views::examples::QUIT_ON_CLOSE);
  base::RunLoop().Run();
  ui::ResourceBundle::CleanupSharedInstance();

  return 0;
}
