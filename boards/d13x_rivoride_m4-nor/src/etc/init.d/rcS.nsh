/****************************************************************************
 *
 * Licensed to the Apache Software Foundation (ASF) under one or more
 * contributor license agreements.  See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.  The
 * ASF licenses this file to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance with the
 * License.  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
 * WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.  See the
 * License for the specific language governing permissions and limitations
 * under the License.
 *
 ****************************************************************************/

#include <nuttx/config.h>
#ifdef CONFIG_D13X_RIVORIDE_M4_NSH

//#if !(defined(CONFIG_BOARDCTL_RESET_CAUSE) && !defined(CONFIG_NSH_DISABLE_RESET_CAUSE))
//#error "Deps: defined(CONFIG_BOARDCTL_RESET_CAUSE) && !defined(CONFIG_NSH_DISABLE_RESET_CAUSE)"
//#endif
//#if !defined(CONFIG_KVDB)
//#error "Deps: defined(CONFIG_KVDB)"
//#endif

set -x
echo "You're running an nsh image."



if [ -e /dev/rodata ]
then
mount -t fatfs /dev/rodata /rodata
fi

if [ -e /dev/data ]
then
mount -t littlefs /dev/data /data
fi
echo "Boot nsh ok"

#endif
