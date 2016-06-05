//////////////////////////////////////////////////////////////////////////////////
// MParallel - Parallel Batch Processor
// Copyright (c) 2016 LoRd_MuldeR <mulder2@gmx.de>. Some rights reserved.
//
// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Public License
// as published by the Free Software Foundation; either version 2
// of the License, or (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software
// Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
//
// http://www.gnu.org/licenses/gpl-2.0.txt
//////////////////////////////////////////////////////////////////////////////////

#pragma once

//Version
#define MPARALLEL_VERSION_MAJOR 1
#define MPARALLEL_VERSION_MINOR 0
#define MPARALLEL_VERSION_PATCH 3

//Support macros
#define __MPARALLEL_VERSION_STR__(X,Y,Z) #X "." #Y "." #Z
#define _MPARALLEL_VERSION_STR_(X,Y,Z) __MPARALLEL_VERSION_STR__(X,Y,Z)
#define MPARALLEL_VERSION_STR _MPARALLEL_VERSION_STR_(MPARALLEL_VERSION_MAJOR,MPARALLEL_VERSION_MINOR,MPARALLEL_VERSION_PATCH)
