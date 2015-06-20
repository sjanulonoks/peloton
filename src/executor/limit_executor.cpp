/**
 * @brief Executor for Limit node.
 *
 * Copyright(c) 2015, CMU
 */

#include "executor/limit_executor.h"

#include "common/logger.h"
#include "common/types.h"
#include "executor/logical_tile.h"
#include "planner/limit_node.h"

namespace nstore {
namespace executor {

/**
 * @brief Constructor
 * @param node  LimitNode plan node corresponding to this executor
 */
LimitExecutor::LimitExecutor(planner::AbstractPlanNode *node,
                             Transaction *transaction)
  : AbstractExecutor(node, transaction){
}

/**
 * @brief Do some basic checks and initialize executor state.
 * @return true on success, false otherwise.
 */
bool LimitExecutor::DInit(){
  assert(children_.size() == 1);

  num_skipped_ = START_OID;
  num_returned_ = START_OID;

  return true;
}

/**
 * @brief Creates logical tiles from the input logical tiles after applying limit.
 * @return true on success, false otherwise.
 */
bool LimitExecutor::DExecute(){

  // Grab data from plan node
  const planner::LimitNode &node = GetNode<planner::LimitNode>();
  const size_t limit = node.GetLimit();
  const size_t offset = node.GetOffset();

  LOG_TRACE("Limit executor \n");

  while(num_returned_ < limit && children_[0]->Execute()){

    std::unique_ptr<LogicalTile> tile(children_[0]->GetOutput());

    for(oid_t tuple_id : *tile){

      // "below" tuples
      if(num_skipped_ < offset){
        tile->InvalidateTuple(tuple_id);
        num_skipped_++;
      }
      // good tuples
      else if(num_returned_ < limit){
        num_returned_++;
      }
      // "above" tuples
      else{
        tile->InvalidateTuple(tuple_id);
      }

    }

    // Avoid returning empty tiles
    if(tile->GetTupleCount() > 0) {
      SetOutput(tile.release());
      return true;
    }

  }

  return false;
}


} /* namespace executor */
} /* namespace nstore */