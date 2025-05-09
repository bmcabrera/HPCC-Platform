/*##############################################################################

    HPCC SYSTEMS software Copyright (C) 2014 HPCC Systems.

    Licensed under the Apache License, Version 2.0 (the "License");
    you may not use this file except in compliance with the License.
    You may obtain a copy of the License at

       http://www.apache.org/licenses/LICENSE-2.0

    Unless required by applicable law or agreed to in writing, software
    distributed under the License is distributed on an "AS IS" BASIS,
    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
    See the License for the specific language governing permissions and
    limitations under the License.
############################################################################## */

#ifndef WORKUNIT_IPP_INCL
#include "seclib.hpp"
#include "dasess.hpp"  /// For IUserDescriptor
#include "dasds.hpp"
#include "workunit.hpp"

/* NB: Some of the classes in this file are also used from casandrawu - which means they all need WORKUNIT_API */

#define SDS_LOCK_TIMEOUT (5*60*1000) // 5 mins
#define WUID_VERSION 2 // recorded in each wuid created, useful for bkwd compat. checks
#define GLOBAL_WORKUNIT "global"

class WORKUNIT_API CLocalWUAppValue : implements IConstWUAppValue, public CInterface
{
    Owned<const IPropertyTree> owner, props;
public:
    IMPLEMENT_IINTERFACE;
    CLocalWUAppValue(const IPropertyTree *_owner, const IPropertyTree *_props);

    virtual const char *queryApplication() const;
    virtual const char *queryName() const;
    virtual const char *queryValue() const;
};


class WORKUNIT_API CLocalWUStatistic : implements IConstWUStatistic, public CInterface
{
    Owned<const IPropertyTree> p;
public:
    IMPLEMENT_IINTERFACE;

    CLocalWUStatistic(const IPropertyTree *p);

    virtual IStringVal & getCreator(IStringVal & str) const override;
    virtual IStringVal & getDescription(IStringVal & str, bool createDefault) const override;
    virtual IStringVal & getFormattedValue(IStringVal & str) const override;
    virtual const char * queryScope() const override;
    virtual StatisticMeasure getMeasure() const override;
    virtual StatisticCreatorType getCreatorType() const override;
    virtual StatisticScopeType getScopeType() const override;
    virtual StatisticKind getKind() const override;
    virtual unsigned __int64 getValue() const override;
    virtual unsigned __int64 getCount() const override;
    virtual unsigned __int64 getMax() const override;
    virtual unsigned __int64 getTimestamp() const override;

};

//==========================================================================================

template <typename T, typename IT, void (* ADD)(const IPropertyTree *p, IArrayOf<IT> &dst)> struct CachedTags
{
    CachedTags(): cached(false) {}
    void load(IPropertyTree* p,const char* xpath)
    {
        if (!cached)
        {
            assertex(tags.length() == 0);
            Owned<IPropertyTreeIterator> r = p->getElements(xpath);
            for (r->first(); r->isValid(); r->next())
            {
                const IPropertyTree &rp = r->query();
                ADD(&rp, tags); // NB: links items it adds
            }
            cached = true;
        }
    }

    void loadBranch(IPropertyTree* p,const char* xpath)
    {
        if (!cached)
        {
            assertex(tags.length() == 0);
            Owned<IPropertyTree> branch = p->getBranch(xpath);
            if (branch)
            {
                Owned<IPropertyTreeIterator> r = branch->getElements("*");
                for (r->first(); r->isValid(); r->next())
                {
                    const IPropertyTree &rp = r->query();
                    ADD(&rp, tags); // NB: links items it adds
                }
            }
            cached = true;
        }
    }

    operator IArrayOf<IT>&() { return tags; }
    unsigned ordinality() const { return tags.ordinality(); }
    IT & item(unsigned i) const { return tags.item(i); }

    void append(const IPropertyTree * p)
    {
        ADD(p, tags); // NB: links p
    }

    void kill()
    {
        cached = false;
        tags.kill();
    }

    bool cached;
    IArrayOf<IT> tags;
};

void addStatistic(const IPropertyTree * p, IArrayOf<IConstWUStatistic> &dst)
{
    dst.append(*new CLocalWUStatistic(&OLINK(*p)));
}

void addWUAppValues(const IPropertyTree *p, IArrayOf<IConstWUAppValue> &dst)
{
    Owned<IPropertyTreeIterator> v = p->getElements("*");
    for (v->first(); v->isValid(); v->next())
        dst.append(*new CLocalWUAppValue(&OLINK(*p), &v->get()));
}

typedef CachedTags<CLocalWUStatistic, IConstWUStatistic, addStatistic> CachedStatistics;
typedef CachedTags<CLocalWUStatistic, IConstWUAppValue, addWUAppValues> CachedWUAppValues;


//==========================================================================================

class WORKUNIT_API CLocalWorkUnit : implements IWorkUnit , implements IExtendedWUInterface, public CInterface
{
    friend StringBuffer &exportWorkUnitToXML(const IConstWorkUnit *wu, StringBuffer &str, bool decodeGraphs, bool includeProgress, bool hidePasswords);
    friend void exportWorkUnitToXMLFile(const IConstWorkUnit *wu, const char * filename, unsigned extraXmlFlags, bool decodeGraphs, bool includeProgress, bool hidePasswords, bool regressionTest);
    friend class CLocalWUGraph;

protected:
    Owned<IPropertyTree> p;
    mutable CriticalSection crit;
    mutable Owned<IWUQuery> query;
    mutable Owned<IWUWebServicesInfo> webServicesInfo;
    mutable Owned<IWorkflowItemIterator> workflowIterator;
    mutable Owned<IPropertyTree> indirectTree;
    mutable bool workflowIteratorCached;
    mutable bool resultsCached;
    mutable unsigned char graphsCached;  // 0 means uncached, 1 means light, 2 means heavy
    mutable bool temporariesCached;
    mutable bool variablesCached;
    mutable bool exceptionsCached;
    mutable bool pluginsCached;
    mutable bool librariesCached;
    mutable bool activitiesCached;
    mutable bool webServicesInfoCached;
    mutable bool roxieQueryInfoCached;
    mutable IArrayOf<IWUPlugin> plugins;
    mutable IArrayOf<IWULibrary> libraries;
    mutable IArrayOf<IWUException> exceptions;
    mutable IArrayOf<IConstWUGraph> graphs;
    mutable IArrayOf<IWUResult> results;
    mutable IArrayOf<IWUResult> temporaries;
    mutable IArrayOf<IWUResult> variables;
    mutable CachedWUAppValues appvalues;
    mutable CachedStatistics statistics;
    mutable Owned<IUserDescriptor> userDesc;
    Mutex locked;
    Owned<ISecManager> secMgr;
    Owned<ISecUser> secUser;

protected:
    
public:
    IMPLEMENT_IINTERFACE;

    CLocalWorkUnit(ISecManager *secmgr, ISecUser *secuser);
    void loadPTree(IPropertyTree *ptree);
    void beforeDispose();
    
    IPropertyTree *getUnpackedTree(bool includeProgress) const;
    IPropertyTree *queryMergedTree() const;

    ISecManager *querySecMgr() { return secMgr.get(); }
    ISecUser *querySecUser() { return secUser.get(); }

    virtual bool aborting() const { return false; }
    virtual void forceReload() {};
    virtual WUAction getAction() const;
    virtual const char *queryActionDesc() const;
    virtual IStringVal & getApplicationValue(const char * application, const char * propname, IStringVal & str) const;
    virtual int getApplicationValueInt(const char * application, const char * propname, int defVal) const;
    virtual IConstWUAppValueIterator & getApplicationValues() const;
    virtual bool hasWorkflow() const;
    virtual unsigned queryEventScheduledCount() const;
    virtual IPropertyTree * queryWorkflowTree() const;
    virtual IConstWorkflowItemIterator * getWorkflowItems() const;
    virtual IWorkflowItemArray * getWorkflowClone() const;
    virtual IConstLocalFileUploadIterator * getLocalFileUploads() const;
    virtual bool requiresLocalFileUpload() const;
    virtual bool getIsQueryService() const;
    virtual const char *queryClusterName() const;
    virtual bool hasDebugValue(const char * propname) const;
    virtual IStringVal & getDebugValue(const char * propname, IStringVal & str) const;
    virtual IStringIterator & getDebugValues() const;
    virtual IStringIterator & getDebugValues(const char *prop) const;
    virtual int getDebugValueInt(const char * propname, int defVal) const;
    virtual __int64 getDebugValueInt64(const char * propname, __int64 defVal) const;
    double getDebugValueReal(const char *propname, double defVal) const;
    virtual bool getDebugValueBool(const char * propname, bool defVal) const;
    virtual unsigned getExceptionCount() const;
    virtual IConstWUExceptionIterator & getExceptions() const;
    virtual IConstWUResult * getGlobalByName(const char * name) const;
    virtual unsigned getGraphCount() const;
    virtual unsigned getSourceFileCount() const;
    virtual unsigned getResultCount() const;
    virtual unsigned getVariableCount() const;
    virtual unsigned getApplicationValueCount() const;
    virtual IConstWUGraphIterator & getGraphs(WUGraphType type) const;
    virtual IConstWUGraphMetaIterator & getGraphsMeta(WUGraphType type) const;
    virtual IConstWUGraph * getGraph(const char *name) const;
    virtual IConstWUGraphProgress * getGraphProgress(const char * name) const;
    virtual WUGraphState queryGraphState(const char *graphName) const;
    virtual void setGraphState(const char *graphName, unsigned wfid, WUGraphState state) const;
    virtual void setNodeState(const char *graphName, WUGraphIDType nodeId, WUGraphState state) const;
    virtual WUGraphState queryNodeState(const char *graphName, WUGraphIDType nodeId) const;
    virtual IWUGraphStats *updateStats(const char *graphName, StatisticCreatorType creatorType, const char * creator, unsigned _wfid, unsigned subgraph, bool merge) const override;
    void clearGraphProgress() const;
    virtual void import(IPropertyTree *wuTree, IPropertyTree *graphProgressTree) {}; //No GraphProgressTree in CLocalWorkUnit.

    virtual const char *queryJobName() const;
    virtual IConstWUPlugin * getPluginByName(const char * name) const;
    virtual IConstWUPluginIterator & getPlugins() const;
    virtual IConstWULibraryIterator & getLibraries() const;
    virtual WUPriorityClass getPriority() const;
    virtual double getCost() const;
    virtual const char *queryPriorityDesc() const;
    virtual int getPriorityLevel() const;
    virtual int getPriorityValue() const;
    virtual IConstWUQuery * getQuery() const;
    virtual bool getRescheduleFlag() const;
    virtual IConstWUResult * getResultByName(const char * name) const;
    virtual IConstWUResult * getResultBySequence(unsigned seq) const;
    virtual IConstWUResult * getQueryResultByName(const char *qname) const;
    virtual unsigned getResultLimit() const;
    virtual IConstWUResultIterator & getResults() const;
    virtual IStringVal & getScope(IStringVal & str) const;
    virtual IStringVal & getWorkunitDistributedAccessToken(IStringVal & tok) const;
    virtual WUState getState() const;
    virtual IStringVal & getStateEx(IStringVal & str) const;
    virtual __int64 getAgentSession() const;
    virtual __int64 getEngineSession() const;
    virtual unsigned getAgentPID() const;
    virtual const char *queryStateDesc() const;
    virtual IConstWUResult * getTemporaryByName(const char * name) const;
    virtual IConstWUResultIterator & getTemporaries() const;
    virtual IConstWUScopeIterator & getScopeIterator(const WuScopeFilter & filter) const override;
    virtual bool getStatistic(stat_type & value, const char * scope, StatisticKind kind) const override;
    virtual IConstWUWebServicesInfo * getWebServicesInfo() const;
    virtual IStringVal & getXmlParams(IStringVal & params, bool hidePasswords) const;
    virtual const IPropertyTree *getXmlParams() const;
    virtual unsigned __int64 getHash() const;
    virtual IStringIterator *getLogs(const char *type, const char *component) const;
    virtual IPropertyTreeIterator *getProcessTypes() const;
    virtual IStringIterator *getProcesses(const char *type) const;
    virtual IPropertyTreeIterator* getProcesses(const char *type, const char *instance) const;
    virtual IStringVal & getSnapshot(IStringVal & str) const;
    virtual ErrorSeverity getWarningSeverity(unsigned code, ErrorSeverity defaultSeverity) const;

    virtual const char *queryUser() const;
    virtual const char *queryWuScope() const;
    virtual IConstWUResult * getVariableByName(const char * name) const;
    virtual IConstWUResultIterator & getVariables() const;
    virtual const char *queryWuid() const;
    virtual bool getRunningGraph(IStringVal &graphName, WUGraphIDType &subId) const;
    virtual bool isProtected() const;
    virtual bool isPausing() const;
    virtual IWorkUnit& lock();
    virtual IConstWorkUnit *unlock();
    virtual void requestAbort();
    virtual unsigned calculateHash(unsigned prevHash);
    virtual void copyWorkUnit(IConstWorkUnit *cached, bool copyStats, bool all);
    virtual IPropertyTree *queryPTree() const;
    virtual unsigned queryFileUsage(const char *filename) const;
    virtual IConstWUFileUsageIterator * getFieldUsage() const;
    virtual bool getFieldUsageArray(StringArray & filenames, StringArray & columnnames, const char * clusterName) const;

    virtual bool getCloneable() const;
    virtual IUserDescriptor * queryUserDescriptor() const;
    virtual unsigned getCodeVersion() const;
    virtual unsigned getWuidVersion() const;
    virtual void getBuildVersion(IStringVal & buildVersion, IStringVal & eclVersion) const;
    virtual IPropertyTreeIterator & getFileIterator() const;
    virtual bool archiveWorkUnit(const char *base,bool del,bool ignoredllerrors,bool deleteOwned,bool exportAssociatedFiles);
    virtual IJlibDateTime & getTimeScheduled(IJlibDateTime &val) const;
    virtual IPropertyTreeIterator & getFilesReadIterator() const;
    virtual void protect(bool protectMode);
    virtual IConstWULibrary * getLibraryByName(const char * name) const;
    virtual unsigned getDebugAgentListenerPort() const;
    virtual IStringVal & getDebugAgentListenerIP(IStringVal &ip) const;
    virtual unsigned getTotalThorTime() const;
    virtual IStringVal & getAbortBy(IStringVal & str) const;
    virtual unsigned __int64 getAbortTimeStamp() const;
    virtual cost_type getExecuteCost() const;
    virtual cost_type getFileAccessCost() const;
    virtual cost_type getCompileCost() const;
    void clearExceptions(const char *source=nullptr);
    void commit();
    IWUException *createException();
    void addProcess(const char *type, const char *instance, unsigned pid, unsigned max, const char *pattern, bool singleLog, const char *log);
    bool setContainerizedProcessInfo(const char *type, const char *instance, const char *podName, const char *containerName, const char *graphName, const char *sequence);
    void setAction(WUAction action);
    void setApplicationValue(const char * application, const char * propname, const char * value, bool overwrite);
    void setApplicationValueInt(const char * application, const char * propname, int value, bool overwrite);
    void incEventScheduledCount();
    void setIsQueryService(bool value);
    void setCloneable(bool value);
    void setIsClone(bool value);
    void setClusterName(const char * value);
    void setCodeVersion(unsigned version, const char * buildVersion, const char * eclVersion);
    void setDebugValue(const char * propname, const char * value, bool overwrite);
    void setDebugValueInt(const char * propname, int value, bool overwrite);
    void setJobName(const char * value);
    void setPriority(WUPriorityClass cls);
    void setCost(double cost);
    void setPriorityLevel(int level);
    void setRescheduleFlag(bool value);
    void setResultLimit(unsigned value);
    void setState(WUState state);
    void setStateEx(const char * text);
    void setAgentSession(__int64 sessionId);
    void setEngineSession(__int64 sessionId);
    bool setDistributedAccessToken(const char * user);
    void setStatistic(StatisticCreatorType creatorType, const char * creator, StatisticScopeType scopeType, const char * scope, StatisticKind kind, const char * optDescription, unsigned __int64 value, unsigned __int64 count, unsigned __int64 maxValue, StatsMergeAction mergeAction);
    void setTracingValue(const char * propname, const char * value);
    void setTracingValueInt(const char * propname, int value);
    void setTracingValueInt64(const char * propname, __int64 value);
    void setUser(const char * value);
    void setWarningSeverity(unsigned code, ErrorSeverity severity);
    void setWuScope(const char * value);
    void setSnapshot(const char * value);
    void setDebugAgentListenerPort(unsigned port);
    void setDebugAgentListenerIP(const char * ip);
    void setXmlParams(const char *params);
    void setXmlParams(IPropertyTree *tree);
    void setHash(unsigned __int64 hash);

    IWorkflowItem* addWorkflowItem(unsigned wfid, WFType type, WFMode mode, unsigned success, unsigned failure, unsigned recovery, unsigned retriesAllowed, unsigned contingencyFor);
    IWorkflowItemIterator * updateWorkflowItems();
    void syncRuntimeWorkflow(IWorkflowItemArray * array);
    void resetWorkflow();
    void schedule();
    void deschedule();
    unsigned addLocalFileUpload(LocalFileUploadType type, char const * source, char const * destination, char const * eventTag);
    IWUResult * updateGlobalByName(const char * name);
    void createGraph(const char * name, const char *label, WUGraphType type, IPropertyTree *xgmml, unsigned wfid);
    IWUQuery * updateQuery();
    IWUWebServicesInfo* updateWebServicesInfo(bool create);

    IWUPlugin * updatePluginByName(const char * name);
    IWULibrary * updateLibraryByName(const char * name);
    virtual IWUResult * updateResultByName(const char * name);
    virtual IWUResult * updateResultBySequence(unsigned seq);
    virtual IWUResult * updateTemporaryByName(const char * name);
    virtual IWUResult * updateVariableByName(const char * name);
    void addFile(const char *fileName, StringArray *clusters, unsigned usageCount, WUFileKind fileKind, const char *graphOwner);
    void noteFileRead(IDistributedFile *file);
    void noteFieldUsage(IPropertyTree * usage);
    void releaseFile(const char *fileName);
    void resetBeforeGeneration();
    void deleteTempFiles(const char *graph, bool deleteOwned, bool deleteJobOwned);
    void deleteTemporaries();
    void setTimeScheduled(const IJlibDateTime &val);
    virtual void subscribe(WUSubscribeOptions options) {};

    virtual bool getSummary(SummaryType type, SummaryMap &map) const override;
    virtual void setSummary(SummaryType type, const SummaryMap &map) override;

// ILocalWorkUnit - used for debugging etc
    void loadXML(const char *xml);
    void serialize(MemoryBuffer &tgt);
    void deserialize(MemoryBuffer &src);

    IWorkUnit &lockRemote(bool commit);
    void unlockRemote();
    void abort();
    bool switchThorQueue(const char *cluster, IQueueSwitcher *qs, const char *item);
    void setAllowedClusters(const char *value);
    IStringVal & getAllowedClusters(IStringVal & str) const;
    void remoteCheckAccess(IUserDescriptor *user, bool writeaccess) const;
    void setAllowAutoQueueSwitch(bool val);
    bool getAllowAutoQueueSwitch() const;
    void setLibraryInformation(const char * name, unsigned interfaceHash, unsigned definitionHash);

    virtual void cleanupAndDelete(bool deldll,bool deleteOwned, const StringArray *deleteExclusions=NULL);
    virtual void setResultInt(const char * name, unsigned sequence, __int64 val)
    {
        Owned<IWUResult> r = updateResult(name, sequence);
        if (r)
        {
            r->setResultInt(val);
            r->setResultStatus(ResultStatusCalculated);
        }
    }
    virtual void setResultUInt(const char * name, unsigned sequence, unsigned __int64 val)
    {
        Owned<IWUResult> r = updateResult(name, sequence);
        if (r)
        {
            r->setResultUInt(val);
            r->setResultStatus(ResultStatusCalculated);
        }
    }
    virtual void setResultReal(const char *name, unsigned sequence, double val)
    {
        Owned<IWUResult> r = updateResult(name, sequence);
        if (r)
        {
            r->setResultReal(val);
            r->setResultStatus(ResultStatusCalculated);
        }
    }
    virtual void setResultVarString(const char * stepname, unsigned sequence, const char *val)
    {
        setResultString(stepname, sequence, strlen(val), val);
    }
    virtual void setResultVarUnicode(const char * stepname, unsigned sequence, UChar const *val)
    {
        setResultUnicode(stepname, sequence, rtlUnicodeStrlen(val), val);
    }
    virtual void setResultString(const char * stepname, unsigned sequence, int len, const char *val)
    {
        doSetResultString(type_string, stepname, sequence, len, val);
    }
    virtual void setResultData(const char * stepname, unsigned sequence, int len, const void *val)
    {
        doSetResultString(type_data, stepname, sequence, len, (const char *)val);
    }
    virtual void setResultRaw(const char * name, unsigned sequence, int len, const void *val)
    {
        Owned<IWUResult> r = updateResult(name, sequence);
        if (r)
        {
            r->setResultRaw(len, val, ResultFormatRaw);
            r->setResultStatus(ResultStatusCalculated);
        }
    }
    virtual void setResultSet(const char * name, unsigned sequence, bool isAll, size32_t len, const void *val, ISetToXmlTransformer *)
    {
        Owned<IWUResult> r = updateResult(name, sequence);
        if (r)
        {
            r->setResultIsAll(isAll);
            r->setResultRaw(len, val, ResultFormatRaw);
            r->setResultStatus(ResultStatusCalculated);
        }
    }
    virtual void setResultUnicode(const char * name, unsigned sequence, int len, UChar const * val)
    {
        Owned<IWUResult> r = updateResult(name, sequence);
        if (r)
        {
            r->setResultUnicode((char const *)val, len);
            r->setResultStatus(ResultStatusCalculated);
        }
    }
    virtual void setResultBool(const char *name, unsigned sequence, bool val)
    {
        Owned<IWUResult> r = updateResult(name, sequence);
        if (r)
        {
            r->setResultBool(val);
            r->setResultStatus(ResultStatusCalculated);
        }
    }
    virtual void setResultDecimal(const char *name, unsigned sequence, int len, int precision, bool isSigned, const void *val)
    {
        Owned<IWUResult> r = updateResult(name, sequence);
        if (r)
        {
            r->setResultDecimal(val, len);
            r->setResultStatus(ResultStatusCalculated);
        }
    }
    virtual void setResultDataset(const char * name, unsigned sequence, size32_t len, const void *val, unsigned numRows, bool extend)
    {
        Owned<IWUResult> r = updateResult(name, sequence);
        if (r)
        {
            __int64 totalRows = numRows;
            if (extend)
            {
                totalRows += r->getResultRowCount();
                r->addResultRaw(len, val, ResultFormatRaw);
            }
            else
                r->setResultRaw(len, val, ResultFormatRaw);

            r->setResultStatus(ResultStatusCalculated);
            r->setResultRowCount(totalRows);
            r->setResultTotalRowCount(totalRows);
        }
    }

    IWUResult *updateResult(const char *name, unsigned sequence)
    {
        Owned <IWUResult> result = updateWorkUnitResult(this, name, sequence);
        if (result)
        {
            SCMStringBuffer rname;
            if (!result->getResultName(rname).length())
                result->setResultName(name);
        }
        return result.getClear();
    }

    void doSetResultString(type_t type, const char *name, unsigned sequence, int len, const char *val)
    {
        Owned<IWUResult> r = updateResult(name, sequence);
        if (r)
        {
            r->setResultString(val, len);
            r->setResultStatus(ResultStatusCalculated);
        }
    }

protected:
    void clearCached(bool clearTree);
    IWUResult *createResult();
    void loadGraphs(bool heavy) const;
    void loadResults() const;
    void loadTemporaries() const;
    void loadVariables() const;
    void loadExceptions() const;
    void loadPlugins() const;
    void loadLibraries() const;
    void loadClusters() const;
    void checkAgentRunning(WUState & state);
    bool hasApplicationValue(const char * application, const char * propname) const;
    bool resolveFilePrefix(StringBuffer & prefix, const char * queue) const;

    // Implemented by derived classes
    virtual IPropertyTree *getGraphProgressTree() const { return NULL; };
    virtual void unsubscribe() {};
    virtual void _lockRemote() {};
    virtual void _unlockRemote() {};
    virtual void _loadFilesRead() const;
    virtual void _loadFilesWritten() const;
    virtual void _loadGraphs(bool heavy) const;
    virtual void _loadResults() const;
    virtual void _loadVariables() const;
    virtual void _loadTemporaries() const;
    virtual void _loadStatistics() const;
    virtual void _loadExceptions() const;
};

class WORKUNIT_API CPersistedWorkUnit : public CLocalWorkUnit, implements IWorkUnitSubscriber
{
public:
    CPersistedWorkUnit(ISecManager *secmgr, ISecUser *secuser) : CLocalWorkUnit(secmgr, secuser)
    {
        abortDirty = true;
        abortState = false;
    }
    virtual void subscribe(WUSubscribeOptions options);
    virtual void unsubscribe();
    virtual bool aborting() const;
protected:
    virtual void notify(WUSubscribeOptions, unsigned, const void *) override { abortDirty = true; }
    Owned<IWorkUnitWatcher> abortWatcher;
    mutable bool abortDirty;
    mutable bool abortState;
};

class WORKUNIT_API CWorkUnitFactory : implements IWorkUnitFactory, public CInterface
{
public:
    IMPLEMENT_IINTERFACE;

    CWorkUnitFactory();
    ~CWorkUnitFactory();

    // interface IWorkUnitFactory - some are left for derived classes

    virtual IWorkUnit * createWorkUnit(const char * app, const char * user, ISecManager *secmgr, ISecUser *secuser);
    virtual void importWorkUnit(const char *zapReportFileName, const char *zapReportPassword,
        const char *importDir, const char *app, const char *user, ISecManager *secMgr, ISecUser *secUser);
    virtual bool deleteWorkUnit(const char * wuid, ISecManager *secmgr, ISecUser *secuser);
    virtual bool deleteWorkUnitEx(const char * wuid, bool throwException, ISecManager *secmgr, ISecUser *secuser);
    virtual IConstWorkUnit * openWorkUnit(const char * wuid, ISecManager *secmgr, ISecUser *secuser, bool expected);
    virtual IWorkUnit * updateWorkUnit(const char * wuid, ISecManager *secmgr, ISecUser *secuser);
    virtual bool restoreWorkUnit(const char *base, const char *wuid, bool restoreAssociated);
    virtual int setTracingLevel(int newlevel);
    virtual IWorkUnit * createNamedWorkUnit(const char * wuid, const char * app, const char *scope, ISecManager *secmgr, ISecUser *secuser);
    virtual IWorkUnit * getGlobalWorkUnit(ISecManager *secmgr, ISecUser *secuser) = 0;
    virtual IConstWorkUnitIterator * getWorkUnitsByOwner(const char * owner, ISecManager *secmgr, ISecUser *secuser) = 0;
    virtual IConstWorkUnitIterator* getWorkUnitsSorted(WUSortField sortorder, // field to sort by
                                                WUSortField *filters,   // NULL or list of fields to filter on (terminated by WUSFterm)
                                                const void *filterbuf,  // (appended) string values for filters
                                                unsigned startoffset,
                                                unsigned maxnum,
                                                __int64 *cachehint,
                                                unsigned *total,
                                                ISecManager *secmgr,
                                                ISecUser *secuser) = 0;
    virtual unsigned numWorkUnits() = 0;
    virtual IConstWorkUnitIterator *getScheduledWorkUnits(ISecManager *secmgr = NULL, ISecUser *secuser = NULL) = 0;
    virtual void descheduleAllWorkUnits(ISecManager *secmgr, ISecUser *secuser);
    virtual IConstQuerySetQueryIterator * getQuerySetQueriesSorted(WUQuerySortField *sortorder, WUQuerySortField *filters, const void *filterbuf,
        unsigned startoffset, unsigned maxnum, __int64 *cachehint, unsigned *total, const MapStringTo<bool> *subset, const MapStringTo<bool> *suspendedByCluster);
    virtual bool isAborting(const char *wuid) const;
    virtual void clearAborting(const char *wuid);
    virtual WUState waitForWorkUnit(const char * wuid, unsigned timeout, bool compiled,  std::list<WUState> expectedStates) = 0;

    virtual StringArray &getUniqueValues(WUSortField field, const char *prefix, StringArray &result) const
    {
        return result; // Default behaviour if we can't implement efficiently is to return empty list
    }

protected:
    void reportAbnormalTermination(const char *wuid, WUState &state, SessionId agent, const char *sessionText);

    // These need to be implemented by the derived classes
    virtual CLocalWorkUnit* _createWorkUnit(const char *wuid, ISecManager *secmgr, ISecUser *secuser) = 0;
    virtual CLocalWorkUnit* _openWorkUnit(const char *wuid, ISecManager *secmgr, ISecUser *secuser) = 0;  // for read access
    virtual CLocalWorkUnit* _updateWorkUnit(const char *wuid, ISecManager *secmgr, ISecUser *secuser) = 0;  // for write access
    virtual bool _restoreWorkUnit(IPTree *pt, const char *wuid) = 0;
};

class WORKUNIT_API CLocalWUGraph : implements IConstWUGraph, public CInterface
{
    const CLocalWorkUnit &owner;
    Owned<IPropertyTree> p;
    mutable Owned<IPropertyTree> graph; // cached copy of graph xgmml
    unsigned wuidVersion;

    void mergeProgress(IPropertyTree &tree, const IPropertyTree &progressTree, const unsigned progressV) const;

public:
    IMPLEMENT_IINTERFACE;
    CLocalWUGraph(const CLocalWorkUnit &owner, IPropertyTree *p);

    virtual IStringVal & getXGMML(IStringVal & ret, bool mergeProgress, bool doFormatStats) const override;
    virtual IStringVal & getName(IStringVal & ret) const override;
    virtual IStringVal & getLabel(IStringVal & ret) const override;
    virtual IStringVal & getTypeName(IStringVal & ret) const override;
    virtual WUGraphType getType() const override;
    virtual WUGraphState getState() const override;
    virtual unsigned getWfid() const override;
    virtual IPropertyTree * getXGMMLTree(bool mergeProgress, bool doFormatStats) const override;
    virtual IPropertyTree * getXGMMLTreeRaw() const override;

    void setName(const char *str);
    void setLabel(const char *str);
    void setType(WUGraphType type);
    void setWfid(unsigned wfid);
    void setXGMML(const char *str);
    void setXGMMLTree(IPropertyTree * tree);
};

class WORKUNIT_API CWuGraphStats : public CInterfaceOf<IWUGraphStats>
{
public:
    CWuGraphStats(StatisticCreatorType _creatorType, const char * _creator, unsigned wfid, const char * _rootScope, unsigned _id, bool _merge);
    virtual void beforeDispose();
    virtual IStatisticGatherer & queryStatsBuilder();
protected:
    virtual IPropertyTree &queryProgressTree() = 0;
    Owned<IStatisticGatherer> collector;
    StringAttr creator;
    StatisticCreatorType creatorType;
    unsigned id;
    bool merge;
};

class WORKUNIT_API CWorkUnitWatcher : public CInterface, implements IWorkUnitWatcher, implements ISDSSubscription
{
protected:
    CriticalSection crit;
    IWorkUnitSubscriber *subscriber; // not linked - it will generally link me
    SubscriptionId abortId, stateId, actionId;
public:
    IMPLEMENT_IINTERFACE;
    CWorkUnitWatcher(IWorkUnitSubscriber *_subscriber, WUSubscribeOptions flags, const char *wuid);
    ~CWorkUnitWatcher();
    void unsubscribe();
    void notify(SubscriptionId id, const char *xpath, SDSNotifyFlags flags, unsigned valueLen, const void *valueData);
};

class WorkUnitWaiter : public CInterface, implements IAbortHandler, implements IWorkUnitSubscriber
{
    Semaphore changed;
    Owned<IWorkUnitWatcher> watcher;
    mutable bool aborted = false;
    mutable bool abortDirty = false;
    StringAttr wuid;
public:
    IMPLEMENT_IINTERFACE;
    WorkUnitWaiter(const char *_wuid, WUSubscribeOptions watchFor) : wuid(_wuid)
    {
        Owned<IWorkUnitFactory> factory = getWorkUnitFactory();
        watcher.setown(factory->getWatcher(this, watchFor, wuid));
        if (watchFor & SubscribeOptionAbort)
            abortDirty = true;
    }
    ~WorkUnitWaiter()
    {
        unsubscribe();
    }
    bool isAborted() const
    {
        if (!aborted && abortDirty)
        {
            Owned<IWorkUnitFactory> factory = getWorkUnitFactory();
            if (factory->isAborting(wuid))
                aborted = true;
            abortDirty = false;
        }
        return aborted;
    }
    bool wait(unsigned timeout)
    {
        return changed.wait(timeout) && !aborted;
    }
    void unsubscribe()
    {
        if (watcher)
        {
            watcher->unsubscribe();
            watcher.clear();
        }
    }
// IWorkUnitSubscriber
    virtual void notify(WUSubscribeOptions flags, unsigned valueLen, const void *valueData) override
    {
        if (SubscribeOptionAbort == flags)
             abortDirty = true;
        changed.signal();
    }
// IAbortHandler
    virtual bool onAbort() override
    {
        aborted = true;
        changed.signal();
        return false;
    }
};

#define PROGRESS_FORMAT_V 2

extern WORKUNIT_API IConstWUGraphProgress *createConstGraphProgress(const char *_wuid, const char *_graphName, IPropertyTree *_progress);
extern WORKUNIT_API  IPropertyTree * pruneBranch(IPropertyTree * from, char const * xpath);

#endif
