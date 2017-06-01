// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.
// See the LICENSE file in the project root for more information.
// ===========================================================================
// File: CodeVersion.h
//
// ===========================================================================


#ifndef CODE_VERSION_H
#define CODE_VERSION_H

#ifdef FEATURE_CODE_VERSIONING

class CodeVersionManager
{
public:
#if defined(DACCESS_COMPILE) || defined(CROSSGEN_COMPILE)
    CodeVersionManager() {}
#else
    CodeVersionManager();
#endif

private:

};

#endif // FEATURE_CODE_VERSIONING

#endif // CODE_VERSION_H
