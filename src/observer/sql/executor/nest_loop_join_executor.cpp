//
// Created by emrick on 2021/10/27.
//

#include "nest_loop_join_executor.h"
#include <vector>

NestLoopJoinExecutor::NestLoopJoinExecutor(ExecutorContext* context, const TupleSchema &output_schema,
                                     Executor *left_executor,
                                     Executor *right_executor,
                                     std::vector<Filter*> condition_filters,
                                     bool ban_all):
                                     Executor(context, output_schema),
                                     left_executor_(left_executor), right_executor_(right_executor),
                                     condition_filters_(std::move(condition_filters)), ban_all_(ban_all) {};

RC NestLoopJoinExecutor::init() {
  RC rc;
  rc = left_executor_->init();
  if(rc != RC::SUCCESS) {
    return rc;
  }
  rc = right_executor_->init();
  if(rc != RC::SUCCESS) {
    return rc;
  }
  return RC::SUCCESS;
}

RC NestLoopJoinExecutor::next(TupleSet &tuple_set, std::vector<Filter*> *filters) {
  tuple_set.set_schema(output_schema_);
  if (ban_all_) {
    return RC::SUCCESS;
  }
  TupleSet left_tuple_set;
  left_executor_->next(left_tuple_set);
  TupleSet right_tuple_set;
  right_executor_->next(right_tuple_set);
  
  std::vector<int> left_tuple_index = output_schema_.index_in(left_tuple_set.get_schema());
  std::vector<int> right_tuple_index = output_schema_.index_in(right_tuple_set.get_schema());

  for (const auto & left_tuple : left_tuple_set.tuples()) {
    for (auto & right_tuple: right_tuple_set.tuples()) {
      bool valid = true;
      if (filters != nullptr) {
        for (auto & tmp_filter : *filters) {
          if (!tmp_filter->filter(left_tuple, left_tuple_set.get_schema(), right_tuple, right_tuple_set.get_schema())) {
            valid = false;
            break;
          }
        }
      }
      if (!valid) { continue; }
      for (auto & self_filter : condition_filters_) {
        if (!self_filter->filter(left_tuple, left_tuple_set.get_schema(), right_tuple, right_tuple_set.get_schema())) {
          valid = false;
          break;
        }
      }
      if (valid) {
        Tuple result_tuple;
        result_tuple.add(left_tuple, left_tuple_index);
        result_tuple.add(right_tuple, right_tuple_index);
        tuple_set.add(std::move(result_tuple));
      }
    }
  }
  return RC::SUCCESS;
}