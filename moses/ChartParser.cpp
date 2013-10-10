// $Id$
// vim:tabstop=2
/***********************************************************************
 Moses - factored phrase-based language decoder
 Copyright (C) 2010 Hieu Hoang

 This library is free software; you can redistribute it and/or
 modify it under the terms of the GNU Lesser General Public
 License as published by the Free Software Foundation; either
 version 2.1 of the License, or (at your option) any later version.

 This library is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 Lesser General Public License for more details.

 You should have received a copy of the GNU Lesser General Public
 License along with this library; if not, write to the Free Software
 Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 ***********************************************************************/

#include "ChartParser.h"
#include "ChartParserCallback.h"
#include "ChartRuleLookupManager.h"
#include "StaticData.h"
#include "TreeInput.h"
#include "Sentence.h"
#include "DecodeGraph.h"
#include "moses/FF/UnknownWordPenaltyProducer.h"
#include "moses/TranslationModel/PhraseDictionary.h"
#include "moses/TranslationModel/CYKPlusParser/ChartLookup.h"

using namespace std;
using namespace Moses;

namespace Moses
{
extern bool g_debug;

ChartParserUnknown::ChartParserUnknown() {}

ChartParserUnknown::~ChartParserUnknown()
{
  RemoveAllInColl(m_unksrcs);
  RemoveAllInColl(m_cacheTargetPhraseCollection);
}

void ChartParserUnknown::Process(const Word &sourceWord, const WordsRange &range, ChartParserCallback &to)
{
  // unknown word, add as trans opt
  const StaticData &staticData = StaticData::Instance();
  const UnknownWordPenaltyProducer *unknownWordPenaltyProducer = staticData.GetUnknownWordPenaltyProducer();

  size_t isDigit = 0;
  if (staticData.GetDropUnknown()) {
    const Factor *f = sourceWord[0]; // TODO hack. shouldn't know which factor is surface
    const StringPiece s = f->GetString();
    isDigit = s.find_first_of("0123456789");
    if (isDigit == string::npos)
      isDigit = 0;
    else
      isDigit = 1;
    // modify the starting bitmap
  }

  Phrase* unksrc = new Phrase(1);
  unksrc->AddWord() = sourceWord;
  Word &newWord = unksrc->GetWord(0);
  newWord.SetIsOOV(true);

  m_unksrcs.push_back(unksrc);

  //TranslationOption *transOpt;
  if (! staticData.GetDropUnknown() || isDigit) {
    // loop
    const UnknownLHSList &lhsList = staticData.GetUnknownLHS();
    UnknownLHSList::const_iterator iterLHS;
    for (iterLHS = lhsList.begin(); iterLHS != lhsList.end(); ++iterLHS) {
      const string &targetLHSStr = iterLHS->first;
      float prob = iterLHS->second;

      // lhs
      //const Word &sourceLHS = staticData.GetInputDefaultNonTerminal();
      Word *targetLHS = new Word(true);

      targetLHS->CreateFromString(Output, staticData.GetOutputFactorOrder(), targetLHSStr, true);
      CHECK(targetLHS->GetFactor(0) != NULL);

      // add to dictionary
      TargetPhrase *targetPhrase = new TargetPhrase();
      Word &targetWord = targetPhrase->AddWord();
      targetWord.CreateUnknownWord(sourceWord);

      // scores
      float unknownScore = FloorScore(TransformScore(prob));

      targetPhrase->GetScoreBreakdown().Assign(unknownWordPenaltyProducer, unknownScore);
      targetPhrase->Evaluate(*unksrc);

      targetPhrase->SetTargetLHS(targetLHS);
      targetPhrase->SetAlignmentInfo("0-0");

      // chart rule
      to.AddPhraseOOV(*targetPhrase, m_cacheTargetPhraseCollection, range);
    } // for (iterLHS
  } else {
    // drop source word. create blank trans opt
    float unknownScore = FloorScore(-numeric_limits<float>::infinity());

    TargetPhrase *targetPhrase = new TargetPhrase();
    // loop
    const UnknownLHSList &lhsList = staticData.GetUnknownLHS();
    UnknownLHSList::const_iterator iterLHS;
    for (iterLHS = lhsList.begin(); iterLHS != lhsList.end(); ++iterLHS) {
      const string &targetLHSStr = iterLHS->first;
      //float prob = iterLHS->second;

      Word *targetLHS = new Word(true);
      targetLHS->CreateFromString(Output, staticData.GetOutputFactorOrder(), targetLHSStr, true);
      CHECK(targetLHS->GetFactor(0) != NULL);

      targetPhrase->GetScoreBreakdown().Assign(unknownWordPenaltyProducer, unknownScore);
      targetPhrase->Evaluate(*unksrc);

      targetPhrase->SetTargetLHS(targetLHS);

      // chart rule
      to.AddPhraseOOV(*targetPhrase, m_cacheTargetPhraseCollection, range);
    }
  }
}

ChartParser::ChartParser(InputType const &source, ChartCellCollectionBase &cells) :
  m_decodeGraphList(StaticData::Instance().GetDecodeGraphs()),
  m_source(source)
{
  const StaticData &staticData = StaticData::Instance();

  staticData.InitializeForInput(source);
  CreateInputPaths(m_source);

  const std::vector<PhraseDictionary*> &dictionaries = staticData.GetPhraseDictionaries();
  m_ruleLookupManagers.reserve(dictionaries.size());
  for (std::vector<PhraseDictionary*>::const_iterator p = dictionaries.begin();
       p != dictionaries.end(); ++p) {

    const PhraseDictionary *dict = *p;
    PhraseDictionary *nonConstDict = const_cast<PhraseDictionary*>(dict);

    ChartRuleLookupManager *lookupMgr = nonConstDict->CreateRuleLookupManager(*this, cells);
    m_ruleLookupManagers.push_back(lookupMgr);

    ChartLookup *newLookupMgr = nonConstDict->GetChartLookup(static_cast<const ChartCellCollection&>(cells));
    m_lookupManagers.push_back(newLookupMgr);
  }

}

ChartParser::~ChartParser()
{
  RemoveAllInColl(m_ruleLookupManagers);
  StaticData::Instance().CleanUpAfterSentenceProcessing(m_source);

  InputPathMatrix::const_iterator iterOuter;
  for (iterOuter = m_inputPathMatrix.begin(); iterOuter != m_inputPathMatrix.end(); ++iterOuter) {
    const std::vector<InputPath*> &outer = *iterOuter;

    std::vector<InputPath*>::const_iterator iterInner;
    for (iterInner = outer.begin(); iterInner != outer.end(); ++iterInner) {
      InputPath *path = *iterInner;
      delete path;
    }
  }
}

void ChartParser::Create(const WordsRange &wordsRange, ChartParserCallback &to)
{
	assert(m_decodeGraphList.size() == m_ruleLookupManagers.size());

  std::vector <DecodeGraph*>::const_iterator iterDecodeGraph;
  std::vector <ChartRuleLookupManager*>::const_iterator iterRuleLookupManagers = m_ruleLookupManagers.begin();
  for (iterDecodeGraph = m_decodeGraphList.begin(); iterDecodeGraph != m_decodeGraphList.end(); ++iterDecodeGraph, ++iterRuleLookupManagers) {
    const DecodeGraph &decodeGraph = **iterDecodeGraph;
    assert(decodeGraph.GetSize() == 1);
    ChartRuleLookupManager &ruleLookupManager = **iterRuleLookupManagers;
    size_t maxSpan = decodeGraph.GetMaxChartSpan();
    if (maxSpan == 0 || wordsRange.GetNumWordsCovered() <= maxSpan) {
      ruleLookupManager.GetChartRuleCollection(wordsRange, to);
    }
  }

  if (wordsRange.GetNumWordsCovered() == 1 && wordsRange.GetStartPos() != 0 && wordsRange.GetStartPos() != m_source.GetSize()-1) {
    bool alwaysCreateDirectTranslationOption = StaticData::Instance().IsAlwaysCreateDirectTranslationOption();
    if (to.Empty() || alwaysCreateDirectTranslationOption) {
      // create unknown words for 1 word coverage where we don't have any trans options
      const Word &sourceWord = m_source.GetWord(wordsRange.GetStartPos());
      m_unknown.Process(sourceWord, wordsRange, to);
    }
  }


  // new
 cerr << "wordsRange=" << wordsRange  << endl;
 const InputPath &path = GetInputPath(wordsRange);
 for (size_t i = 0; i < m_lookupManagers.size(); ++i) {
	 ChartLookup &newLookupMgr = *m_lookupManagers[i];

	  if (wordsRange.GetNumWordsCovered() == 1) {
	    newLookupMgr.Init(path);
	  }
	  else {
		newLookupMgr.Extend(path);
	  }
  }

}

void ChartParser::CreateInputPaths(const InputType &input)
{
  size_t inputSize = input.GetSize();
  m_inputPathMatrix.resize(inputSize);

  CHECK(input.GetType() == SentenceInput || input.GetType() == TreeInputType);
  for (size_t phraseSize = 1; phraseSize <= inputSize; ++phraseSize) {
    for (size_t startPos = 0; startPos < inputSize - phraseSize + 1; ++startPos) {
      size_t endPos = startPos + phraseSize -1;
      vector<InputPath*> &vec = m_inputPathMatrix[startPos];

      WordsRange range(startPos, endPos);
      Phrase subphrase(input.GetSubString(WordsRange(startPos, endPos)));
      const NonTerminalSet &labels = input.GetLabelSet(startPos, endPos);

      InputPath *node;
      if (range.GetNumWordsCovered() == 1) {
        node = new InputPath(subphrase, labels, range, NULL, NULL);
        vec.push_back(node);
      } else {
        const InputPath &prevNode = GetInputPath(startPos, endPos - 1);
        node = new InputPath(subphrase, labels, range, &prevNode, NULL);
        vec.push_back(node);
      }
    }
  }

  // create postfixes
  for (size_t startPos = 0; startPos < inputSize; ++startPos) {
	  for (size_t endPos = startPos; endPos < inputSize; ++endPos) {
		  InputPath &path = GetInputPath(startPos, endPos);
		  cerr << "path=" <<path << endl;
		  CreateInputPathPostfixes(path);
	  }
  }
}

void ChartParser::CreateInputPathPostfixes(InputPath &path)
{
  const WordsRange &range = path.GetWordsRange();
  size_t endPos = range.GetEndPos();
  for (size_t startPos = range.GetStartPos() + 1; startPos <= endPos; ++startPos) {
	  const InputPath &postfixPath = GetInputPath(startPos, endPos);
	  cerr << "postfixPath=" << postfixPath << endl;
	  path.AddPostfixOf(&postfixPath);
  }

}

const InputPath &ChartParser::GetInputPath(const WordsRange &range) const
{
  return GetInputPath(range.GetStartPos(), range.GetEndPos());
}

const InputPath &ChartParser::GetInputPath(size_t startPos, size_t endPos) const
{
  size_t offset = endPos - startPos;
  CHECK(offset < m_inputPathMatrix[startPos].size());
  return *m_inputPathMatrix[startPos][offset];
}

InputPath &ChartParser::GetInputPath(size_t startPos, size_t endPos)
{
  size_t offset = endPos - startPos;
  CHECK(offset < m_inputPathMatrix[startPos].size());
  return *m_inputPathMatrix[startPos][offset];
}
/*
const Sentence &ChartParser::GetSentence() const {
  const Sentence &sentence = static_cast<const Sentence&>(m_source);
  return sentence;
}
*/
size_t ChartParser::GetSize() const
{
  return m_source.GetSize();
}

long ChartParser::GetTranslationId() const
{
  return m_source.GetTranslationId();
}
} // namespace Moses
