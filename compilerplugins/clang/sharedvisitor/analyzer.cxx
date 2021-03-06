/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the LibreOffice project.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#include "clang/AST/ASTConsumer.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/FrontendAction.h"
#include "clang/Tooling/Tooling.h"
#include "llvm/ADT/StringExtras.h"

#include <cstddef>
#include <cstring>
#include <iostream>
#include <memory>
#include <fstream>
#include <set>

#include "config_clang.h"
#include "../check.hxx"
#include "../check.cxx"

using namespace std;

using namespace clang;
using namespace llvm;

using namespace loplugin;

// Info about a Traverse* function in a plugin.
struct TraverseFunctionInfo
{
    string name;
    string argument;
    bool hasPre = false;
    bool hasPost = false;
};

struct TraverseFunctionInfoLess
{
    bool operator()( const TraverseFunctionInfo& l, const TraverseFunctionInfo& r ) const
    {
        return l.name < r.name;
    }
};

static set< TraverseFunctionInfo, TraverseFunctionInfoLess > traverseFunctions;

class CheckFileVisitor
    : public RecursiveASTVisitor< CheckFileVisitor >
{
public:
    bool VisitCXXRecordDecl(CXXRecordDecl *Declaration);

    bool TraverseNamespaceDecl(NamespaceDecl * decl)
    {
        // Skip non-LO namespaces the same way FilteringPlugin does.
        if( !ContextCheck( decl ).Namespace( "loplugin" ).GlobalNamespace()
            && !ContextCheck( decl ).AnonymousNamespace())
        {
            return true;
        }
        return RecursiveASTVisitor<CheckFileVisitor>::TraverseNamespaceDecl(decl);
    }
};

static bool inheritsPluginClassCheck( const Decl* decl )
{
    return bool( DeclCheck( decl ).Class( "FilteringPlugin" ).Namespace( "loplugin" ).GlobalNamespace())
        || bool( DeclCheck( decl ).Class( "FilteringRewritePlugin" ).Namespace( "loplugin" ).GlobalNamespace());
}

static TraverseFunctionInfo findOrCreateTraverseFunctionInfo( StringRef name )
{
    TraverseFunctionInfo info;
    info.name = name;
    auto foundInfo = traverseFunctions.find( info );
    if( foundInfo != traverseFunctions.end())
    {
        info = move( *foundInfo );
        traverseFunctions.erase( foundInfo );
    }
    return info;
}

static bool foundSomething;

bool CheckFileVisitor::VisitCXXRecordDecl( CXXRecordDecl* decl )
{
    if( !isDerivedFrom( decl, inheritsPluginClassCheck ))
        return true;

    if( decl->getName() == "FilteringPlugin" || decl->getName() == "FilteringRewritePlugin" )
        return true;

    cout << "# This file is autogenerated. Do not modify." << endl;
    cout << "# Generated by compilerplugins/clang/sharedvisitor/analyzer.cxx ." << endl;
    cout << "InfoVersion:1" << endl;
    cout << "ClassName:" << decl->getName().str() << endl;
    traverseFunctions.clear();
    for( const CXXMethodDecl* method : decl->methods())
    {
        if( !method->getDeclName().isIdentifier())
            continue;
        if( method->isStatic() || method->getAccess() != AS_public )
            continue;
        if( method->getName().startswith( "Visit" ))
        {
            if( method->getNumParams() == 1 )
            {
                cout << "VisitFunctionStart" << endl;
                cout << "VisitFunctionName:" << method->getName().str() << endl;
                cout << "VisitFunctionArgument:"
                    << method->getParamDecl( 0 )->getTypeSourceInfo()->getType().getAsString()
                    << endl;
                cout << "VisitFunctionEnd" << endl;
            }
            else
            {
                cerr << "Unhandled Visit* function: " << decl->getName().str()
                     << "::" << method->getName().str() << endl;
                abort();
            }
        }
        else if( method->getName().startswith( "Traverse" ))
        {
            if( method->getNumParams() == 1 )
            {
                TraverseFunctionInfo traverseInfo = findOrCreateTraverseFunctionInfo( method->getName());
                traverseInfo.argument = method->getParamDecl( 0 )->getTypeSourceInfo()->getType().getAsString();
                traverseFunctions.insert( move( traverseInfo ));
            }
            else
            {
                cerr << "Unhandled Traverse* function: " << decl->getName().str()
                     << "::" << method->getName().str() << endl;
                abort();
            }
        }
        else if( method->getName().startswith( "PreTraverse" ))
        {
            TraverseFunctionInfo traverseInfo = findOrCreateTraverseFunctionInfo( method->getName().substr( 3 ));
            traverseInfo.hasPre = true;
            traverseFunctions.insert( move( traverseInfo ));
        }
        else if( method->getName().startswith( "PostTraverse" ))
        {
                TraverseFunctionInfo traverseInfo = findOrCreateTraverseFunctionInfo( method->getName().substr( 4 ));
                traverseInfo.hasPost = true;
                traverseFunctions.insert( move( traverseInfo ));
        }
        else if( method->getName() == "shouldVisitTemplateInstantiations" )
            cout << "ShouldVisitTemplateInstantiations:1" << endl;
        else if (method->getName() == "shouldVisitImplicitCode")
            cout << "ShouldVisitImplicitCode:1" << endl;
        else if( method->getName().startswith( "WalkUp" ))
        {
            cerr << "WalkUp function not supported for shared visitor: " << decl->getName().str()
                 << "::" << method->getName().str() << endl;
            abort();
        }
    }

    for( const auto& traverseFunction : traverseFunctions )
    {
        cout << "TraverseFunctionStart" << endl;
        cout << "TraverseFunctionName:" << traverseFunction.name << endl;
        cout << "TraverseFunctionArgument:" << traverseFunction.argument << endl;
        cout << "TraverseFunctionHasPre:" << traverseFunction.hasPre << endl;
        cout << "TraverseFunctionHasPost:" << traverseFunction.hasPost << endl;
        cout << "TraverseFunctionEnd" << endl;
    }

    cout << "InfoEnd" << endl;
    foundSomething = true;
    return true;
}

class FindNamedClassConsumer
    : public ASTConsumer
{
public:
    virtual void HandleTranslationUnit(ASTContext& context) override
    {
        visitor.TraverseDecl( context.getTranslationUnitDecl());
    }
private:
    CheckFileVisitor visitor;
};

class FindNamedClassAction
    : public ASTFrontendAction
    {
public:
    virtual unique_ptr<ASTConsumer> CreateASTConsumer( CompilerInstance&, StringRef ) override
    {
        return unique_ptr<ASTConsumer>( new FindNamedClassConsumer );
    }
};


string readSourceFile( const char* filename )
{
    string contents;
    ifstream stream( filename );
    if( !stream )
    {
        cerr << "Failed to open: " << filename << endl;
        exit( 1 );
    }
    string line;
    bool hasIfdef = false;
    while( getline( stream, line ))
    {
        // TODO add checks that it's e.g. not "#ifdef" ?
        if( line.find( "#ifndef LO_CLANG_SHARED_PLUGINS" ) == 0 )
            hasIfdef = true;
        contents += line;
        contents += '\n';
    }
    if( stream.eof() && hasIfdef )
        return contents;
    return "";
}

int main(int argc, char** argv)
{
    vector< string > args;
    int i = 1;
    for( ; i < argc; ++ i )
    {
        constexpr std::size_t prefixlen = 5; // strlen("-arg=");
        if (std::strncmp(argv[i], "-arg=", prefixlen) != 0)
        {
            break;
        }
        args.push_back(argv[i] + prefixlen);
    }
    SmallVector< StringRef, 20 > clangflags;
    SplitString( CLANGFLAGS, clangflags );
    args.insert( args.end(), clangflags.begin(), clangflags.end());
    args.insert(
        args.end(),
        {   // These must match LO_CLANG_ANALYZER_PCH_CXXFLAGS in Makefile-clang.mk .
            "-I" BUILDDIR "/config_host" // plugin sources use e.g. config_global.h
#ifdef LO_CLANG_USE_ANALYZER_PCH
            ,
            "-include-pch", // use PCH with Clang headers to speed up parsing/analysing
            BUILDDIR "/compilerplugins/clang/sharedvisitor/clang.pch"
#endif
        });
    for( ; i < argc; ++ i )
    {
        string contents = readSourceFile(argv[i]);
        if( contents.empty())
            continue;
        foundSomething = false;
#if CLANG_VERSION >= 100000
        if( !tooling::runToolOnCodeWithArgs( std::unique_ptr<FindNamedClassAction>(new FindNamedClassAction), contents, args, argv[ i ] ))
#else
        if( !tooling::runToolOnCodeWithArgs( new FindNamedClassAction, contents, args, argv[ i ] ))
#endif
        {
            cerr << "Failed to analyze: " << argv[ i ] << endl;
            return 2;
        }
        if( !foundSomething )
        {
            // there's #ifndef LO_CLANG_SHARED_PLUGINS in the source, but no class matched
            cerr << "Failed to find code: " << argv[ i ] << endl;
            return 2;
        }
    }
    return 0;
}
