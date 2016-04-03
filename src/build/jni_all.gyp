# Copyright (c) 2012 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

{
  'targets': [
  	{
  		 'target_name': 'JniAll',
	      'type': 'none',
	      'dependencies': [
	        #'../base/jni_test.gyp:*',
	        '../base/android/jni_generator/jni_generator.gyp:*',
	      ],
  	},
  ],
}
