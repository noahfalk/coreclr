// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.
// See the LICENSE file in the project root for more information.
//
// fileloadlevel.h


#ifndef FILELOADLEVEL_H_
#define FILELOADLEVEL_H_

enum FileLoadLevel
{
    // These states are tracked by DomainFile::FileLoadLock

    // Note: This enum must match the static array DomainFile::fileLoadLevelName[]
    //       which contains the printable names of the enum values 

    // Note that semantics here are description is the LAST step done, not what is 
    // currently being done.

    FILE_LOAD_CREATE,
    FILE_LOAD_BEGIN,
    FILE_LOAD_FIND_NATIVE_IMAGE,
    FILE_LOAD_VERIFY_NATIVE_IMAGE_DEPENDENCIES,
    FILE_LOAD_ALLOCATE,
    FILE_LOAD_ADD_DEPENDENCIES,
    FILE_LOAD_PRE_LOADLIBRARY,
    FILE_LOAD_LOADLIBRARY,
    FILE_LOAD_POST_LOADLIBRARY,
    FILE_LOAD_EAGER_FIXUPS,
    FILE_LOAD_VTABLE_FIXUPS,
    FILE_LOAD_DELIVER_EVENTS,
    FILE_LOADED,                    // Loaded by not yet active
    FILE_LOAD_VERIFY_EXECUTION,
    FILE_ACTIVE                     // Fully active (constructors run & security checked)
};

#endif // FILELOADLEVEL_H_
