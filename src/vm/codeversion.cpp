// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.
// See the LICENSE file in the project root for more information.
// ===========================================================================
// File: CodeVersion.cpp
//
// ===========================================================================



#include "common.h"
#include "codeversion.h"


#ifdef FEATURE_CODE_VERSIONING
#ifndef DACCESS_COMPILE
// Called at AppDomain construction
CodeVersionManager::CodeVersionManager()
{
    LIMITED_METHOD_CONTRACT;
}

#endif // DACCESS_COMPILE
#endif // FEATURE_CODE_VERSIONING

