/*-------------------------------------------------------------------------
 *
 * transaction.cpp
 * file description
 *
 * Copyright(c) 2015, CMU
 *
 * /n-store/src/common/transaction.cpp
 *
 *-------------------------------------------------------------------------
 */

#include "common/transaction.h"
#include "common/synch.h"
#include "common/logger.h"

#include <chrono>
#include <thread>
#include <iomanip>

namespace nstore {

//===--------------------------------------------------------------------===//
// Transaction
//===--------------------------------------------------------------------===//

void Transaction::RecordInsert(ItemPointer location) {
  auto& manager = catalog::Manager::GetInstance();
  storage::TileGroup *tile_group = static_cast<storage::TileGroup *>(manager.locator[location.block]);
  inserted_tuples[tile_group].push_back(location.offset);
}

void Transaction::RecordDelete(ItemPointer location) {
  auto& manager = catalog::Manager::GetInstance();
  storage::TileGroup *tile_group = static_cast<storage::TileGroup *>(manager.locator[location.block]);
  deleted_tuples[tile_group].push_back(location.offset);
}

bool Transaction::HasInsertedTuples(storage::TileGroup* tile_group) const {
  auto tile_group_itr = inserted_tuples.find(tile_group);
  if(tile_group_itr != inserted_tuples.end() && !tile_group_itr->second.empty())
    return true;

  return false;
}

bool Transaction::HasDeletedTuples(storage::TileGroup* tile_group) const {
  auto tile_group_itr = deleted_tuples.find(tile_group);
  if(tile_group_itr != deleted_tuples.end() && !tile_group_itr->second.empty())
    return true;

  return false;
}

const std::map<storage::TileGroup*, std::vector<oid_t> >& Transaction::GetInsertedTuples() {
  return inserted_tuples;
}

const std::map<storage::TileGroup*, std::vector<oid_t> >& Transaction::GetDeletedTuples() {
  return deleted_tuples;
}

void Transaction::IncrementRefCount() {
  ++ref_count;
}

void Transaction::DecrementRefCount() {
  assert(ref_count > 0);

  // DROP transaction when ref count reaches 1
  if (ref_count.fetch_sub(1) == 1) {
    delete this;
  }
}

std::ostream& operator<<(std::ostream& os, const Transaction& txn) {

  os << "\tTxn :: @" <<  &txn << " ID : " << std::setw(4) << txn.txn_id
      << " Commit ID : " << std::setw(4) << txn.cid
      << " Last Commit ID : " << std::setw(4) << txn.last_cid;

  if(txn.next == nullptr) {
    os << " Next : " << std::setw(4) << txn.next;
  }
  else {
    os << " Next : " << std::setw(4) << txn.next->txn_id;
  }

  os << " Ref count : " << std::setw(4) << txn.ref_count <<  "\n";
  return os;
}

//===--------------------------------------------------------------------===//
// Transaction Manager
//===--------------------------------------------------------------------===//

// Get entry in table
Transaction *TransactionManager::GetTransaction(txn_id_t txn_id) {
  if(txn_table.count(txn_id) != 0) {
    return txn_table.at(txn_id);
  }
  return nullptr;
}

// Begin a new transaction
Transaction *TransactionManager::BeginTransaction(){
  Transaction *next_txn = GetInstance().BuildTransaction();
  return next_txn;
}

Transaction *TransactionManager::BuildTransaction(){
  return new Transaction(GetNextTransactionId(), GetLastCommitId());
}

std::vector<Transaction *> TransactionManager::GetCurrentTransactions(){
  std::vector<Transaction *> txns;

  for(auto entry : GetInstance().txn_table)
    txns.push_back(entry.second);
  return txns;
}

bool TransactionManager::IsValid(txn_id_t txn_id){
  return (txn_id < GetInstance().next_txn_id);
}

void TransactionManager::EndTransaction(Transaction *txn, bool sync){

  // XXX LOG :: record commit entry

  // erase entry in transaction table
  txn_table.erase(txn->txn_id);
}

//===--------------------------------------------------------------------===//
// Commit Processing
//===--------------------------------------------------------------------===//

TransactionManager& TransactionManager::GetInstance() {
  static TransactionManager txn_manager;
  return txn_manager;
}

void TransactionManager::BeginCommitPhase(Transaction *txn){

  // successor in the transaction list will point to us
  txn->IncrementRefCount();

  while (true) {

    // try to append to the pending transaction list
    if (atomic_cas<Transaction *>(&last_txn->next, nullptr, txn)) {

      // the last transaction pointer also points to us
      txn->IncrementRefCount();

      // assign cid to the txn
      txn->cid = last_txn->cid + 1;

      auto tmp = last_txn;
      last_txn = txn;

      // drop a reference to previous last transaction pointer
      tmp->DecrementRefCount();

      return;
    }
  }

}

void TransactionManager::CommitModifications(Transaction *txn, bool sync){

  // (A) commit inserts
  auto inserted_tuples = txn->GetInsertedTuples();
  for(auto entry : inserted_tuples) {
    storage::TileGroup *tile_group = entry.first;

    for(auto tuple_slot : entry.second)
      tile_group->CommitInsertedTuple(tuple_slot, txn->cid);
  }

  // (B) commit deletes
  auto deleted_tuples = txn->GetDeletedTuples();
  for(auto entry : deleted_tuples) {
    storage::TileGroup *tile_group = entry.first;
    for(auto tuple_slot : entry.second)
      tile_group->CommitDeletedTuple(tuple_slot, txn->txn_id, txn->cid);
  }

  // XXX LOG :: record commit entry

}

void TransactionManager::CommitPendingTransactions(std::vector<Transaction *>& pending_txns, Transaction *txn){

  // add ourself to the list
  pending_txns.push_back(txn);

  // commit all pending transactions
  auto current_txn = txn->next;

  while (current_txn != nullptr && current_txn->waiting_to_commit == true) {

    // try to increment last finished cid
    if (atomic_cas(&last_cid, current_txn->cid - 1, current_txn->cid)) {

      // if that worked, add transaction to list
      pending_txns.push_back(current_txn);
      LOG_TRACE("Pending Txn  : %lu \n", current_txn->txn_id);

      current_txn = current_txn->next;
      continue;
    }
    // it did not work, so some other txn must have squeezed in
    // so, stop processing commit dependencies
    else {
      break;
    }

  }

}

std::vector<Transaction*> TransactionManager::EndCommitPhase(Transaction * txn, bool sync){
  std::vector<Transaction *> txn_list;
  auto& txn_mgr = GetInstance();

  // try to increment last commit id
  if (atomic_cas(&last_cid, txn->cid - 1, txn->cid)) {

    LOG_TRACE("update lcid worked : %lu \n", txn->txn_id);

    // everything went fine and the txn was committed
    // if that worked, commit all pending transactions
    txn_mgr.CommitPendingTransactions(txn_list, txn);

  }
  // it did not work, so add to waiting list
  // some other transaction with lower commit id will commit us later
  else {

    LOG_TRACE("add to wait list : %lu \n", txn->txn_id);

    txn->waiting_to_commit = true;

    // make sure that the transaction we are waiting for has not finished
    // before we could add ourselves to the list of pending transactions
    // we try incrementing the last finished cid again
    if (atomic_cas(&last_cid, txn->cid - 1, txn->cid)) {

      // it worked on the second try
      txn->waiting_to_commit = false;

      txn_mgr.CommitPendingTransactions(txn_list, txn);
    }
  }

  // clear txn entry in txn table
  txn_mgr.EndTransaction(txn, sync);

  return std::move(txn_list);
}


void TransactionManager::CommitTransaction(Transaction *txn, bool sync){
  assert(txn != nullptr);

  // validate txn id
  if (!IsValid(txn->txn_id)) {
    throw TransactionException("Transaction not found in transaction table : " + std::to_string(txn->txn_id));
  }

  auto& txn_mgr = GetInstance();

  // begin commit phase : get cid and add to transaction list
  txn_mgr.BeginCommitPhase(txn);

  // commit all modifications
  txn_mgr.CommitModifications(txn, sync);

  // end commit phase : increment last_cid and process pending txns if needed
  std::vector<Transaction *> committed_txns = txn_mgr.EndCommitPhase(txn, sync);

  // XXX LOG : group commit entry

}

//===--------------------------------------------------------------------===//
// Abort Processing
//===--------------------------------------------------------------------===//

void TransactionManager::WaitForCurrentTransactions() const{

  std::vector<txn_id_t> current_txns;

  // record all currently running transactions
  for(auto entry : txn_table)
    current_txns.push_back(entry.first);


  // block until all current txns are finished
  while (true) {

    // remove all finished txns from list
    for(auto txn_id : current_txns) {
      if(txn_table.count(txn_id) == 0) {
        auto location = std::find(current_txns.begin(), current_txns.end(), txn_id);
        if (location != current_txns.end())
          current_txns.erase(location);
      }
    }

    // all transactions in waiting list finished ?
    if(current_txns.empty())
      break;

    // sleep for some time
    std::chrono::milliseconds sleep_time(10); // 10 ms
    std::this_thread::sleep_for(sleep_time);
  }

}

void TransactionManager::AbortTransaction(Transaction *txn){

  // (A) rollback inserts
  auto inserted_tuples = txn->GetInsertedTuples();
  for(auto entry : inserted_tuples) {
    storage::TileGroup *tile_group = entry.first;

    for(auto tuple_slot : entry.second)
      tile_group->AbortInsertedTuple(tuple_slot);
  }

  // (B) rollback deletes
  auto deleted_tuples = txn->GetDeletedTuples();
  for(auto entry : txn->GetDeletedTuples()) {
    storage::TileGroup *tile_group = entry.first;

    for(auto tuple_slot : entry.second)
      tile_group->AbortDeletedTuple(tuple_slot);
  }

  GetInstance().EndTransaction(txn, false);

  // XXX LOG :: record abort entry
}

} // End nstore namespace