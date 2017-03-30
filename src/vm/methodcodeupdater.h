// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.
// See the LICENSE file in the project root for more information.
// ===========================================================================
// File: MethodCodeUpdater.h
//
// ===========================================================================


#ifndef METHOD_CODE_UPDATER_H
#define METHOD_CODE_UPDATER_H

#ifdef FEATURE_TIERED_COMPILATION

// TieredCompilationManager determines which methods should be recompiled and
// how they should be recompiled to best optimize the running code. It then
// handles logistics of getting new code created and installed.
class MethodCodeUpdater
{
public:
#if defined(DACCESS_COMPILE) || defined(CROSSGEN_COMPILE)
    MethodCodeUpdater() {}
#else
    MethodCodeUpdater();
#endif

private:

};

#endif // FEATURE_TIERED_COMPILATION

#endif // METHOD_CODE_UPDATER
