//
// C++ Implementation: dvisourcesplitter
//
// Author: Jeroen Wijnhout <Jeroen.Wijnhout@kdemail.net>, (C) 2004
//
// Copyright: See COPYING file that comes with this distribution
//

#include <qdir.h>

#include <kdebug.h>

#include "dvisourcesplitter.h"

DVI_SourceFileSplitter::DVI_SourceFileSplitter(const QString &srclink, const QString &dviFile)
{
  QString filepart = srclink, linepart;
  bool possibleNumberMixUp = false; //if sourcefilename starts with a number 
                                    //then there could be a mix up, i.e. src:123file.tex 
                                    //line 123 and file.tex or line 12 and 3file.tex?
  
  kdDebug() << "DVI_SourceSplitter: srclink " << srclink << endl;
  //remove src: if necessary
  if ( filepart.left(4) == "src:" ) filepart = srclink.mid(4);
    
  //split first
  Q_UINT32 max = filepart.length(), i = 0;
  for(i=0; i<max; ++i) if ( !filepart[i].isDigit()) break;
  linepart = filepart.left(i);
  filepart = filepart.mid(i);
    
  //check for number mix up
  if ( filepart[0] != ' ' && (linepart.length() != 1) ) possibleNumberMixUp = true;

  //remove a spaces  
  filepart = filepart.stripWhiteSpace();
  linepart = linepart.stripWhiteSpace();
  
  kdDebug() << "DVI_SourceSplitter: filepart " << filepart << " linepart " << linepart << endl;
 
  //test if the file exists
  m_fileInfo.setFile(QFileInfo(dviFile).dir(true), filepart);
  bool fiExists = m_fileInfo.exists();
  
  //if it doesn't exist, but adding ".tex" 
  if ( !fiExists && QFileInfo(m_fileInfo.absFilePath() + ".tex").exists() )
    m_fileInfo.setFile(m_fileInfo.absFilePath() + ".tex");
    
  //if that doesn't help either, perhaps the file started with a 
  //number: move the numbers from the sourceline to the filename
  //one by one (also try to add .tex repeatedly)
  if ( possibleNumberMixUp && !fiExists )
  {
    QFileInfo tempInfo(m_fileInfo);
    QString tempFileName = tempInfo.fileName();
    Q_UINT32 index, maxindex = linepart.length();
    bool found = false;
    for ( index = 1; index < maxindex; ++index)
    {
      tempInfo.setFile(linepart.right(index) + tempFileName);
      kdDebug() << "DVI_SourceSplitter: trying " << tempInfo.fileName() << endl;
      if ( tempInfo.exists() ) { found = true; break;}
      tempInfo.setFile(linepart.right(index) + tempFileName + ".tex");
      kdDebug() << "DVI_SourceSplitter: trying " << tempInfo.fileName() << endl;
      if ( tempInfo.exists() ) { found = true; break;}
    }
    
    if (found)
    {
      m_fileInfo = tempInfo;
      linepart = linepart.left(maxindex - index);
    }
  }
  
  bool ok;
  m_line = linepart.toInt(&ok);
  if (!ok) m_line = 0;
  
  kdDebug() << "DVI_SourceSplitter: result: file " << m_fileInfo.absFilePath() << " line " << m_line << endl;
}
