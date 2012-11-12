/***********************************************************************
Moses - factored phrase-based language decoder
Copyright (C) 2006 University of Edinburgh

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

#ifndef moses_PhraseDictionaryMultiModel_h
#define moses_PhraseDictionaryMultiModel_h

#include "PhraseDictionary.h"
#include "PhraseDictionaryMemory.h"
#ifndef WIN32
#include "CompactPT/PhraseDictionaryCompact.h"
#endif


#include <boost/unordered_map.hpp>
#include "StaticData.h"
#include "TargetPhrase.h"
#include "Util.h"
#include "UserMessage.h"

#define WITH_DLIB
#ifdef WITH_DLIB
#include "/home/rico/smtworkspace/online/lbfgs/dlib-17.47/dlib/optimization.h"
#endif

namespace Moses
{

  struct multiModelStatistics {
    TargetPhrase *targetPhrase;
    std::vector<std::vector<float> > p;
  };

  struct multiModelStatisticsOptimization: multiModelStatistics {
    size_t f;
  };

class OptimizationObjective;

/** Implementation of a virtual phrase table constructed from multiple component phrase tables.
 */
class PhraseDictionaryMultiModel: public PhraseDictionary
{
#ifdef WITH_DLIB
friend class CrossEntropy;
#endif

public:
  PhraseDictionaryMultiModel(size_t m_numScoreComponent, PhraseDictionaryFeature* feature);
  ~PhraseDictionaryMultiModel();
  bool Load(const std::vector<FactorType> &input
            , const std::vector<FactorType> &output
            , const std::vector<std::string> &files
            , const std::vector<float> &weight
            , size_t tableLimit
            , const LMList &languageModels
            , float weightWP);
  virtual void CollectSufficientStatistics(const Phrase& src, std::map<std::string,multiModelStatistics*>* allStats) const;
  virtual TargetPhraseCollection* CreateTargetPhraseCollectionLinearInterpolation(std::map<std::string,multiModelStatistics*>* allStats, std::vector<std::vector<float> > &multimodelweights) const;
  std::vector<std::vector<float> > getWeights(size_t numWeights, bool normalize) const;
  std::vector<float> normalizeWeights(std::vector<float> &weights) const;
  void CacheForCleanup(TargetPhraseCollection* tpc);
  void CleanUp(const InputType &source);
  virtual std::vector<float> MinimizePerplexity(std::vector<std::pair<std::string, std::string> > &phrase_pair_vector);
#ifdef WITH_DLIB
  std::vector<float> Optimize(OptimizationObjective * ObjectiveFunction, size_t numModels);
#endif
  // functions below required by base class
  virtual const TargetPhraseCollection* GetTargetPhraseCollection(const Phrase& src) const;
  virtual void InitializeForInput(InputType const&) {
    /* Don't do anything source specific here as this object is shared between threads.*/
  }
  ChartRuleLookupManager *CreateRuleLookupManager(const InputType&, const ChartCellCollectionBase&);

protected:
  std::string m_mode;
  std::vector<PhraseDictionary*> m_pd;
  std::vector<float> m_weight;
  const LMList *m_languageModels;
  float m_weightWP;
  std::vector<FactorType> m_input;
  std::vector<FactorType> m_output;
  size_t m_numModels;
  size_t m_componentTableLimit;
  PhraseDictionaryFeature* m_feature_load;

  typedef std::vector<TargetPhraseCollection*> PhraseCache;
#ifdef WITH_THREADS
  boost::mutex m_sentenceMutex;
  typedef std::map<boost::thread::id, PhraseCache> SentenceCache;
#else
  typedef PhraseCache SentenceCache;
#endif
  SentenceCache m_sentenceCache;

};

#ifdef WITH_DLIB
class OptimizationObjective 
{
public:

    virtual double operator() ( const dlib::matrix<double,0,1>& arg) const = 0;
};

class CrossEntropy: public OptimizationObjective
{
public:

    CrossEntropy (
        std::vector<multiModelStatisticsOptimization*> &optimizerStats,
        PhraseDictionaryMultiModel * model,
        size_t iFeature
    )
    {
        m_optimizerStats = optimizerStats;
        m_model = model;
        m_iFeature = iFeature;
    }

    double operator() ( const dlib::matrix<double,0,1>& arg) const
    {
        double total = 0.0;
        double n = 0.0;
        std::vector<float> weight_vector (m_model->m_numModels);

        for (int i=0; i < arg.nr(); i++) {
            weight_vector[i] = arg(i);
        }
        if (m_model->m_mode == "interpolate") {
            weight_vector = m_model->normalizeWeights(weight_vector);
        }

        for ( std::vector<multiModelStatisticsOptimization*>::const_iterator iter = m_optimizerStats.begin(); iter != m_optimizerStats.end(); ++iter ) {
            multiModelStatisticsOptimization* statistics = *iter;
            size_t f = statistics->f;

            double score;
            score = std::inner_product(statistics->p[m_iFeature].begin(), statistics->p[m_iFeature].end(), weight_vector.begin(), 0.0);

            total -= (FloorScore(TransformScore(score))/TransformScore(2))*f;
            n += f;
        }
        return total/n;
    }

protected:
    std::vector<multiModelStatisticsOptimization*> m_optimizerStats;
    PhraseDictionaryMultiModel * m_model;
    size_t m_iFeature;
};
#endif

} // end namespace

#endif