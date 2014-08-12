/*************************************************************************************
*  Copyright (C) 2014 by Pavel Petrushkov <onehundredof@gmail.com>                  *
*                                                                                   *
*  This program is free software; you can redistribute it and/or                    *
*  modify it under the terms of the GNU General Public License                      *
*  as published by the Free Software Foundation; either version 2                   *
*  of the License, or (at your option) any later version.                           *
*                                                                                   *
*  This program is distributed in the hope that it will be useful,                  *
*  but WITHOUT ANY WARRANTY; without even the implied warranty of                   *
*  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the                    *
*  GNU General Public License for more details.                                     *
*                                                                                   *
*  You should have received a copy of the GNU General Public License                *
*  along with this program; if not, write to the Free Software                      *
*  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA   *
*************************************************************************************/

#include "golangparsejob.h"

#include <language/backgroundparser/urlparselock.h>
#include <language/backgroundparser/parsejob.h>
#include <language/duchain/duchainlock.h>
#include <language/duchain/duchainutils.h>
#include <language/duchain/duchain.h>
#include <language/duchain/parsingenvironment.h>
#include <language/duchain/problem.h>
#include <interfaces/ilanguage.h>

#include <QReadLocker>

#include "parsesession.h"
#include "duchain/declarationbuilder.h"
#include "duchain/usebuilder.h"

using namespace KDevelop;

GoParseJob::GoParseJob(const KDevelop::IndexedString& url, KDevelop::ILanguageSupport* languageSupport): ParseJob(url, languageSupport)
{
}

void GoParseJob::run()
{
   kDebug() << "GoParseJob succesfully created for document " << document(); 

   UrlParseLock urlLock(document());

    if (abortRequested() || !isUpdateRequired(ParseSession::languageString())) {
        return;
    }

    ProblemPointer p = readContents();
    if(p) {
	 return;
    }

    QByteArray code = contents().contents;
    while(code.endsWith('\0'))
	code.chop(1);
    
    //ParseSession session(QString(contents().contents).toUtf8(), priority());
    ParseSession session(code, parsePriority());
    
    session.setCurrentDocument(document());
    session.setFeatures(minimumFeatures());
    
    if(abortRequested())
      return;

    ReferencedTopDUContext context;
    {
        DUChainReadLocker lock;
        context = DUChainUtils::standardContextForUrl(document().toUrl());
    }
    
    //ParsingEnvironmentFilePointer filep = context->parsingEnvironmentFile();
    
    if (context) {
        translateDUChainToRevision(context);
        context->setRange(RangeInRevision(0, 0, INT_MAX, INT_MAX));
    }
    kDebug() << "Job features: " << minimumFeatures();
    kDebug() << "Job priority: " << parsePriority();
    
    kDebug() << document();
    bool result = session.startParsing();
    //this is useful for testing parser, comment it for actual working
    //Q_ASSERT(result);
    //When switching between files(even if they are not modified) KDevelop decides they need to be updated
    //and calls parseJob with VisibleDeclarations feature
    //so for now feature, identifying import will be AllDeclarationsAndContexts, without Uses
    bool forExport = false;
    if((minimumFeatures() & TopDUContext::AllDeclarationsContextsAndUses) == TopDUContext::AllDeclarationsAndContexts)
	forExport = true;
    //kDebug() << contents().contents;
    //Force parse priority 0 in opened documents
    //(if document was parsed as import before opening it's priority will not be 0)
    if(!forExport)
	setParsePriority(0);
    if(result)
    {
	QReadLocker parseLock(languageSupport()->language()->parseLock());
	
	if(abortRequested())
	  return abortJob();
	//kDebug() << QString(contents().contents);
	DeclarationBuilder builder(&session, forExport);
	context = builder.build(document(), session.ast(), context);
	
	if(!forExport)
	{
	    go::UseBuilder useBuilder(&session);
	    useBuilder.buildUses(session.ast());
	}
	//still not sure if we need this
	//session.reparseImporters(context);
	
    }
    if(!context){
        DUChainWriteLocker lock;
	ParsingEnvironmentFile* file = new ParsingEnvironmentFile(document());
	file->setLanguage(ParseSession::languageString());
        context = new TopDUContext(document(), RangeInRevision(0, 0, INT_MAX, INT_MAX), file);
	DUChain::self()->addDocumentChain(context);
    }
	
    setDuChain(context);
    {
        DUChainWriteLocker lock;
	context->setFeatures(minimumFeatures());
        ParsingEnvironmentFilePointer file = context->parsingEnvironmentFile();
	Q_ASSERT(file);
	DUChain::self()->updateContextEnvironment(context->topContext(), file.data());
    }
    highlightDUChain();
    
    if(result)
      kDebug() << "===Success===" << document().str();
    else
      kDebug() << "===Failed===" << document().str();
}