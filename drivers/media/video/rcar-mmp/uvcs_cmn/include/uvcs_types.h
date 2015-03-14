/*************************************************************************/ /*
 UVCS Common

 Copyright (C) 2013 Renesas Electronics Corporation

 License        Dual MIT/GPLv2

 The contents of this file are subject to the MIT license as set out below.

 Permission is hereby granted, free of charge, to any person obtaining a copy
 of this software and associated documentation files (the "Software"), to deal
 in the Software without restriction, including without limitation the rights
 to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 copies of the Software, and to permit persons to whom the Software is
 furnished to do so, subject to the following conditions:

 The above copyright notice and this permission notice shall be included in
 all copies or substantial portions of the Software.

 Alternatively, the contents of this file may be used under the terms of
 the GNU General Public License Version 2 ("GPL") in which case the provisions
 of GPL are applicable instead of those above.

 If you wish to allow use of your version of this file only under the terms of
 GPL, and not to allow others to use your version of this file under the terms
 of the MIT license, indicate your decision by deleting the provisions above
 and replace them with the notice and other provisions required by GPL as set
 out in the file called "GPL-COPYING" included in this distribution. If you do
 not delete the provisions above, a recipient may use your version of this file
 under the terms of either the MIT license or GPL.

 This License is also included in this distribution in the file called
 "MIT-COPYING".

 EXCEPT AS OTHERWISE STATED IN A NEGOTIATED AGREEMENT: (A) THE SOFTWARE IS
 PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING
 BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
 PURPOSE AND NONINFRINGEMENT; AND (B) IN NO EVENT SHALL THE AUTHORS OR
 COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.


 GPLv2:
 If you wish to use this file under the terms of GPL, following terms are
 effective.

 This program is free software; you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation; version 2 of the License.

 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with this program; if not, write to the Free Software
 Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
*/ /*************************************************************************/

#ifndef UVCS_TYPES_H
#define UVCS_TYPES_H
/******************************************************************************/
/*                      INCLUDE FILES                                         */
/******************************************************************************/


/******************************************************************************/
/*                      MACROS/DEFINES                                        */
/******************************************************************************/
#if !defined(NULL)
#if !defined(__cplusplus)
# define NULL ((void *)0)
#else
# define NULL (0)
#endif
#endif

#define UVCS_NOEL_HWPROC_DATA		(32uL)

/******************************************************************************/
/*               TYPE DEFINITION                                              */
/******************************************************************************/
typedef unsigned char				UVCS_U8 ;
typedef unsigned long				UVCS_U32 ;
typedef signed long					UVCS_S32 ;
typedef void*						UVCS_PTR ;

typedef enum {
	UVCS_FALSE	= 0,
	UVCS_TRUE	= 1
}	UVCS_BOOL ;

typedef enum {
	UVCS_RTN_OK						= 0x00L,
	UVCS_RTN_INVALID_HANDLE			= 0x01L,
	UVCS_RTN_INVALID_STATE			= 0x02L,
	UVCS_RTN_PARAMETER_ERROR		= 0x03L,
	UVCS_RTN_NOT_SUPPORTED			= 0x04L,
	UVCS_RTN_NOT_CONFIGURED			= 0x05L,
	UVCS_RTN_NOT_INITIALISE			= 0x06L,
	UVCS_RTN_ALREADY_INITIALISED	= 0x07L,
	UVCS_RTN_SYSTEM_ERROR			= 0x08L,
	UVCS_RTN_BUSY					= 0x09L,
	UVCS_RTN_CONTINUE				= 0x0AL
}	UVCS_RESULT ;


#endif /* UVCS_TYPES_H */
