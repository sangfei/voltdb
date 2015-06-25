/* This file is part of VoltDB.
 * Copyright (C) 2008-2015 VoltDB Inc.
 *
 * This file contains original code and/or modifications of original code.
 * Any modifications made by VoltDB Inc. are licensed under the following
 * terms and conditions:
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as
 * published by the Free Software Foundation, either version 3 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Affero General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with VoltDB.  If not, see <http://www.gnu.org/licenses/>.
 */
/* Copyright (C) 2008 by H-Store Project
 * Brown University
 * Massachusetts Institute of Technology
 * Yale University
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT
 * IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */

#include <list>
#include <vector>
#include <utility>

#include "mergereceiveexecutor.h"
#include "common/debuglog.h"
#include "common/common.h"
#include "common/tabletuple.h"
#include "plannodes/receivenode.h"
#include "execution/VoltDBEngine.h"
#include "execution/ProgressMonitorProxy.h"
#include "executors/executorutil.h"
#include "storage/table.h"
#include "storage/tablefactory.h"
#include "storage/tableiterator.h"
#include "storage/tableutil.h"
#include "plannodes/orderbynode.h"
#include "plannodes/limitnode.h"


namespace voltdb {

namespace {

typedef std::vector<TableTuple>::const_iterator tuple_iterator;
typedef std::pair<tuple_iterator, tuple_iterator> tuple_range;
typedef std::list<tuple_range>::iterator range_iterator;

range_iterator min_tuple_range(std::list<tuple_range>& partitions, TupleComparer comp) {
    assert(!partitions.empty());

    range_iterator smallest_range = partitions.begin();
    range_iterator rangeIt = smallest_range;
    range_iterator rangeEnd = partitions.end();
    ++rangeIt;
    for (; rangeIt != rangeEnd; ++rangeIt) {
        assert(rangeIt->first != rangeIt->second);
        if (comp(*rangeIt->first, *smallest_range->first)) {
            smallest_range = rangeIt;
        }
    }
    return smallest_range;
}

void merge_sort(const std::vector<TableTuple>& tuples,
    std::vector<int64_t>& partitionTupleCounts,
    TupleComparer comp,
    int limit,
    int offset,
    TempTable* output_table,
    ProgressMonitorProxy pmp) {

    if (partitionTupleCounts.empty()) {
        return;
    }

    size_t nonEmptyPartitions = partitionTupleCounts.size();

    // Vector to hold pairs of iterators denoting the range of tuples
    // for a given partition
    std::list<tuple_range> partitions;
    tuple_iterator begin = tuples.begin();
    for (size_t i = 0; i < nonEmptyPartitions; ++i) {
        tuple_iterator end = begin + partitionTupleCounts[i];
        partitions.push_back(std::make_pair(begin, end));
        begin = end;
        assert( i != nonEmptyPartitions -1 || end == tuples.end());
    }

    int tupleCnt = 0;
    int tupleSkipped = 0;
    while (true) {
        range_iterator rangeIt = partitions.begin();
        // remove partitions that are empty
        while (rangeIt != partitions.end()) {
            if (rangeIt->first == rangeIt->second) {
                rangeIt = partitions.erase(rangeIt);
                --nonEmptyPartitions;
            } else {
                ++rangeIt;
            }
        }
        if (nonEmptyPartitions == 1) {
            // copy the remaining tuples to the output
            range_iterator rangeIt = partitions.begin();
            while (rangeIt->first != rangeIt->second && (limit == -1 || tupleCnt < limit)) {
                TableTuple tuple = *rangeIt->first;
                ++rangeIt->first;
                if (tupleSkipped++ < offset) {
                    continue;
                }
                output_table->insertTupleNonVirtual(tuple);
                pmp.countdownProgress();
            }
            return;
        }

        // Find the range that has the next tuple to be inserted
        range_iterator minRangeIt = min_tuple_range(partitions, comp);
        // copy the first tuple to the output
        TableTuple tuple = *minRangeIt->first;
        output_table->insertTupleNonVirtual(tuple);
        pmp.countdownProgress();
        // advance the iterator
        ++minRangeIt->first;
    }
}

}

MergeReceiveExecutor::MergeReceiveExecutor(VoltDBEngine *engine, AbstractPlanNode* abstract_node)
    : AbstractExecutor(engine, abstract_node), m_limit_node(NULL), m_tmpInputTable(NULL)
{ }

bool MergeReceiveExecutor::p_init(AbstractPlanNode* abstract_node,
                             TempTableLimits* limits)
{
    VOLT_TRACE("init OrderByMerge Executor");

    // Create output table based on output schema from the plan
    setTempOutputTable(limits);

    // Create a temp table to collect tuples from multiple partitions
    m_tmpInputTable = TableFactory::getTempTable(m_abstractNode->databaseId(),
                                                              "tempInput",
                                                              m_abstractNode->generateTupleSchema(),
                                                              m_tmpOutputTable->getColumnNames(),
                                                              limits);

    // inline OrderByPlanNode
    m_orderby_node = dynamic_cast<OrderByPlanNode*>(abstract_node->
                                     getInlinePlanNode(PLAN_NODE_TYPE_ORDERBY));
    assert(m_orderby_node != NULL);

    // pickup an inlined limit, if one exists
    m_limit_node = dynamic_cast<LimitPlanNode*>(m_orderby_node->
                                     getInlinePlanNode(PLAN_NODE_TYPE_LIMIT));

    #if defined(VOLT_LOG_LEVEL)
    #if VOLT_LOG_LEVEL<=VOLT_LEVEL_TRACE
        const std::vector<AbstractExpression*>& sortExprs = m_orderby_node->getSortExpressions();
        for (int i = 0; i < sortExprs.size(); ++i) {
            VOLT_TRACE("Sort key[%d]:\n%s", i, sortExprs[i]->debug(true).c_str());
        }
    #endif
    #endif

    return true;
}

bool MergeReceiveExecutor::p_execute(const NValueArray &params) {
    int loadedDeps = 0;

    // iterate over dependencies and merge them into the temp table.
    // The assumption is that the dependencies results are are sorted.
    int64_t previousTupleCount = 0;
    std::vector<int64_t> partitionTupleCounts;

    //
    // OPTIMIZATION: NESTED LIMIT
    int limit = -1;
    int offset = 0;
    if (m_limit_node != NULL) {
        m_limit_node->getLimitAndOffsetByReference(params, limit, offset);
    }

    do {
        loadedDeps = m_engine->loadNextDependency(m_tmpInputTable);
        int currentTupleCount = m_tmpInputTable->activeTupleCount();
        if (currentTupleCount != previousTupleCount) {
            partitionTupleCounts.push_back(currentTupleCount - previousTupleCount);
            previousTupleCount = currentTupleCount;
        }
    } while (loadedDeps > 0);

    // Unload tuples into a vector to be merge-sorted
    VOLT_TRACE("Running MergeReceive '%s'", m_abstractNode->debug().c_str());
    VOLT_TRACE("Input Table PreSort:\n '%s'", m_tmpInputTable->debug().c_str());
    TableIterator iterator = m_tmpInputTable->iterator();
    TableTuple tuple(m_tmpInputTable->schema());
    std::vector<TableTuple> xs;
    xs.reserve(m_tmpInputTable->activeTupleCount());

    ProgressMonitorProxy pmp(m_engine, this);
    while (iterator.next(tuple))
    {
        pmp.countdownProgress();
        assert(tuple.isActive());
        xs.push_back(tuple);
    }

    // Merge Sort
    TupleComparer comp(m_orderby_node->getSortExpressions(), m_orderby_node->getSortDirections());
    merge_sort(xs, partitionTupleCounts, comp, limit, offset, m_tmpOutputTable, pmp);

    VOLT_TRACE("Result of MergeReceive:\n '%s'", m_tmpOutputTable->debug().c_str());

    cleanupInputTempTable(m_tmpInputTable);

    return true;
}

}