// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.
// See the LICENSE file in the project root for more information.
// ===========================================================================
// File: MethodCodeUpdater.CPP
//
// ===========================================================================



#include "common.h"
#include "excep.h"
#include "log.h"
#include "methodcodeupdater.h"


#ifdef FEATURE_TIERED_COMPILATION
#ifndef DACCESS_COMPILE
// Called at AppDomain construction
MethodCodeUpdater::MethodCodeUpdater()
{
    LIMITED_METHOD_CONTRACT;
}

#endif // DACCESS_COMPILE
#endif // FEATURE_TIERED_COMPILATION

