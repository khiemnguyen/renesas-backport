#
# Copyright(c) 2013 Renesas Electronics Corporation
# RENESAS ELECTRONICS CONFIDENTIAL AND PROPRIETARY.
# This program must be used solely for the purpose for which
# it was furnished by Renesas Electronics Corporation. No part of this
# program may be reproduced or disclosed to others, in any
# form, without the prior written permission of Renesas Electronics
# Corporation.
#
#@License       Dual MIT/GPLv2
# 
# The contents of this file are subject to the MIT license as set out below.
# 
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:
# 
# The above copyright notice and this permission notice shall be included in
# all copies or substantial portions of the Software.
# 
# Alternatively, the contents of this file may be used under the terms of
# the GNU General Public License Version 2 ("GPL") in which case the provisions
# of GPL are applicable instead of those above.
# 
# If you wish to allow use of your version of this file only under the terms of
# GPL, and not to allow others to use your version of this file under the terms
# of the MIT license, indicate your decision by deleting the provisions above
# and replace them with the notice and other provisions required by GPL as set
# out in the file called "GPL-COPYING" included in this distribution. If you do
# not delete the provisions above, a recipient may use your version of this file
# under the terms of either the MIT license or GPL.
# 
# This License is also included in this distribution in the file called
# "MIT-COPYING".
# 
# EXCEPT AS OTHERWISE STATED IN A NEGOTIATED AGREEMENT: (A) THE SOFTWARE IS
# PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING
# BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
# PURPOSE AND NONINFRINGEMENT; AND (B) IN NO EVENT SHALL THE AUTHORS OR
# COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
# IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
# CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
### ###########################################################################

# Add Extensions of REL for each environment.
#

SUPPORT_EXTENSION_REL ?= 1

ifeq ($(SUPPORT_EXTENSION_REL),1)
$(eval $(call BothConfigC,SUPPORT_EXTENSION_REL,))

# Add compile option for debug log
# begin
ENABLE_REL_STD_0001 ?= 1
# end
# Add for logging memory usage
# begin
SUPPORT_GET_MEMINFO ?= 1
# end

# Add for EDM trace log
# begin
ifeq ($(SUPPORT_EDM_TRACE),1)
PVRSRV_DUMP_MK_TRACE := 1
PVRSRV_USSE_EDM_STATUS_DEBUG := 1
endif
# end

# Add to call SGX HW Recovery
# begin
ENABLE_REL_STD_0016 ?= 1
# end

# Add compile option for debug log
# begin
$(eval $(call TunableBothConfigC,ENABLE_REL_STD_0001,))
ifeq ($(ENABLE_REL_STD_0001),1)
$(eval $(call KernelConfigC,DEBUG_LINUX_MEMORY_ALLOCATIONS,))
endif
# end
# Add for logging memory usage
# begin
$(eval $(call TunableBothConfigC,SUPPORT_GET_MEMINFO,))
ifeq ($(SUPPORT_GET_MEMINFO),1)
$(eval $(call KernelConfigC,DEBUG_LINUX_MEMORY_ALLOCATIONS,))
endif
# end

# Add to call SGX HW Recovery
# begin
$(eval $(call TunableBothConfigC,ENABLE_REL_STD_0016,))
# end

#
endif
