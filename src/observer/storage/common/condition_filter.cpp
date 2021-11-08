/* Copyright (c) 2021 Xie Meiyi(xiemeiyi@hust.edu.cn) and OceanBase and/or its affiliates. All rights reserved.
miniob is licensed under Mulan PSL v2.
You can use this software according to the terms and conditions of the Mulan PSL v2.
You may obtain a copy of Mulan PSL v2 at:
         http://license.coscl.org.cn/MulanPSL2
THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
See the Mulan PSL v2 for more details. */

//
// Created by Wangyunlai on 2021/5/7.
//

#include <stddef.h>
#include "condition_filter.h"
#include "record_manager.h"
#include "common/log/log.h"
#include "storage/common/table.h"
#include "common/lang/bitmap.h"

using namespace common;

ConditionFilter::~ConditionFilter()
{}

DefaultConditionFilter::DefaultConditionFilter()
{
  left_.is_attr = false;
  left_.attr_length = 0;
  left_.attr_offset = 0;
  left_.value = nullptr;

  right_.is_attr = false;
  right_.attr_length = 0;
  right_.attr_offset = 0;
  right_.value = nullptr;
}
DefaultConditionFilter::~DefaultConditionFilter()
{}

RC DefaultConditionFilter::init(Table *table, const ConDesc &left, const ConDesc &right, AttrType attr_type, CompOp comp_op)
{
  if ((attr_type < CHARS || attr_type > FLOATS) && !left.is_null) {
    LOG_ERROR("Invalid condition with unsupported attribute type: %d", attr_type);
    return RC::INVALID_ARGUMENT;
  }

  if (comp_op < EQUAL_TO || comp_op >= NO_OP) {
    LOG_ERROR("Invalid condition with unsupported compare operation: %d", comp_op);
    return RC::INVALID_ARGUMENT;
  }

  table_ = table;
  left_ = left;
  right_ = right;
  attr_type_ = attr_type;
  comp_op_ = comp_op;
  return RC::SUCCESS;
}

// TODO(wq): 这个函数后续需要更多的检验和转换
RC DefaultConditionFilter::init(Table &table, const Condition &condition)
{
  const TableMeta &table_meta = table.table_meta();
  ConDesc left;
  ConDesc right;

  AttrType type_left = UNDEFINED;
  AttrType type_right = UNDEFINED;

  if (1 == condition.left_is_attr) {
    left.is_attr = true;
    const FieldMeta *field_left = table_meta.field(condition.left_attr.attribute_name);
    if (nullptr == field_left) {
      LOG_WARN("No such field in condition. %s.%s", table.name(), condition.left_attr.attribute_name);
      return RC::SCHEMA_FIELD_MISSING;
    }
    left.attr_index = table_meta.field_index(condition.left_attr.attribute_name);
    left.attr_length = field_left->len();
    left.attr_offset = field_left->offset();

    left.value = nullptr;

    type_left = field_left->type();
  } else {
    left.is_attr = false;
    if (condition.left_value.isnull) {
      left.is_null = true;
    } else {
      left.is_null = false;
      left.value = condition.left_value.data;  // 校验type 或者转换类型
      type_left = condition.left_value.type;
      if (condition.right_is_attr && table_meta.field(condition.right_attr.attribute_name) != nullptr && 
          table_meta.field(condition.right_attr.attribute_name)->type() == DATES &&
          type_left == CHARS) {
        type_left = DATES;
      }
    }
    left.attr_length = 0;
    left.attr_offset = 0;
  }

  if (1 == condition.right_is_attr) {
    right.is_attr = true;
    const FieldMeta *field_right = table_meta.field(condition.right_attr.attribute_name);
    if (nullptr == field_right) {
      LOG_WARN("No such field in condition. %s.%s", table.name(), condition.right_attr.attribute_name);
      return RC::SCHEMA_FIELD_MISSING;
    }
    right.attr_index = table_meta.field_index(condition.right_attr.attribute_name);
    right.attr_length = field_right->len();
    right.attr_offset = field_right->offset();
    type_right = field_right->type();

    right.value = nullptr;
  } else {
    right.is_attr = false;
    if (condition.right_value.isnull) {
      right.is_null = true;
    } else {
      right.is_null = false;
      right.value = condition.right_value.data;
      type_right = condition.right_value.type;
      if (condition.left_is_attr && table_meta.field(condition.left_attr.attribute_name) != nullptr && 
          table_meta.field(condition.left_attr.attribute_name)->type() == DATES &&
          type_right == CHARS) {
        type_right = DATES;
      }
    }
    right.attr_length = 0;
    right.attr_offset = 0;
  }
  
  // 注意:如果到这里函数还没有返回，能继续执行，说明保证如果conditon中attr字段格式一定能和table_meta匹配
  if (!condition.left_is_attr && !condition.left_value.isnull && condition.right_is_attr && table_meta.field(condition.right_attr.attribute_name)->type() == DATES) {
    if (!theGlobalDateUtil()->Check_and_format_date(left.value) == RC::SUCCESS) {
      LOG_WARN("date type filter condition schema mismatch.");
      return  RC::SCHEMA_FIELD_TYPE_MISMATCH;
    }
  }
  if (!condition.right_is_attr && !condition.right_value.isnull && condition.left_is_attr && table_meta.field(condition.left_attr.attribute_name)->type() == DATES) {
    if (!theGlobalDateUtil()->Check_and_format_date(right.value) == RC::SUCCESS) {
      LOG_WARN("date type filter condition schema mismatch.");
      return  RC::SCHEMA_FIELD_TYPE_MISMATCH;
    }
  }

  // 校验和转换
  //  if (!field_type_compare_compatible_table[type_left][type_right]) {
  //    // 不能比较的两个字段， 要把信息传给客户端
  //    return RC::SCHEMA_FIELD_TYPE_MISMATCH;
  //  }
  // NOTE：这里没有实现不同类型的数据比较，比如整数跟浮点数之间的对比
  // 但是选手们还是要实现。这个功能在预选赛中会出现
  if (!left.is_null && !right.is_null && type_left != type_right) {
    return RC::SCHEMA_FIELD_TYPE_MISMATCH;
  }
  
  if ((condition.comp == CompOp::IS || condition.comp == CompOp::IS_NOT) && !right.is_null) {
    return RC::SCHEMA_FIELD_TYPE_MISMATCH;
  }
  return init(&table, left, right, type_left, condition.comp);
}

bool compare_result(int cmp_result, CompOp comp_op) {
  switch (comp_op) {
    case EQUAL_TO:
      return 0 == cmp_result;
    case LESS_EQUAL:
      return cmp_result <= 0;
    case NOT_EQUAL:
      return cmp_result != 0;
    case LESS_THAN:
      return cmp_result < 0;
    case GREAT_EQUAL:
      return cmp_result >= 0;
    case GREAT_THAN:
      return cmp_result > 0;
    default:
      return false;
  }
}

bool DefaultConditionFilter::filter(const Record &rec) const
{
  char *left_value = nullptr;
  char *right_value = nullptr;

  common::Bitmap null_bitmap((char *)rec.data, table_->table_meta().field_num());
  if (left_.is_attr) {  // value
    if (null_bitmap.get_bit(left_.attr_index)) {
      left_value = nullptr;
    } else {
      left_value = (char *)(rec.data + left_.attr_offset);
    }
  } else {
    if (left_.is_null) {
      left_value = nullptr;
    } else {
      left_value = (char *)left_.value;
    }
  }

  if (right_.is_attr) {
    if (null_bitmap.get_bit(right_.attr_index)) {
      right_value = nullptr;
    } else {
      right_value = (char *)(rec.data + right_.attr_offset);
    }
  } else {
    if (right_.is_null) {
      right_value = nullptr;
    } else {
      right_value = (char *)right_.value;
    }
  }

  // 1. 左右值其中有一个null
  if (left_value == nullptr || right_value == nullptr) {
    if (left_value == nullptr && right_value == nullptr) {
      if (comp_op_ == CompOp::IS) {
        return true;
      }
    }
    if (left_value != nullptr && right_value == nullptr) {
      if (comp_op_ == CompOp::IS_NOT) {
        return true;
      }
    }
    return false;
  }

  // 2. 左右值都不是null
  int cmp_result = 0;
  switch (attr_type_) {
    case CHARS: {  // 字符串都是定长的，直接比较
      // 按照C字符串风格来定
      cmp_result = strcmp(left_value, right_value);
    } break;
    case INTS: {
      // 没有考虑大小端问题
      // 对int和float，要考虑字节对齐问题,有些平台下直接转换可能会跪
      int left = *(int *)left_value;
      int right = *(int *)right_value;
      cmp_result = left - right;
    } break;
    case FLOATS: {
      float left = *(float *)left_value;
      float right = *(float *)right_value;
      cmp_result = (int)(left - right);
    } break;
    case DATES: {  // 字符串日期已经被格式化了，可以直接比较
      // 按照C字符串风格来定
      cmp_result = strcmp(left_value, right_value);
    } break;
    default: {
    }
  }

 return compare_result(cmp_result, comp_op_);
}

CompositeConditionFilter::~CompositeConditionFilter()
{
  if (memory_owner_) {
    delete[] filters_;
    filters_ = nullptr;
  }
}

RC CompositeConditionFilter::init(const ConditionFilter *filters[], int filter_num, bool own_memory)
{
  filters_ = filters;
  filter_num_ = filter_num;
  memory_owner_ = own_memory;
  return RC::SUCCESS;
}
RC CompositeConditionFilter::init(const ConditionFilter *filters[], int filter_num)
{
  return init(filters, filter_num, false);
}

RC CompositeConditionFilter::init(Table &table, const Condition *conditions, int condition_num)
{
  if (condition_num == 0) {
    return RC::SUCCESS;
  }
  if (conditions == nullptr) {
    return RC::INVALID_ARGUMENT;
  }

  RC rc = RC::SUCCESS;
  ConditionFilter **condition_filters = new ConditionFilter *[condition_num];
  for (int i = 0; i < condition_num; i++) {
    DefaultConditionFilter *default_condition_filter = new DefaultConditionFilter();
    rc = default_condition_filter->init(table, conditions[i]);
    if (rc != RC::SUCCESS) {
      delete default_condition_filter;
      for (int j = i - 1; j >= 0; j--) {
        delete condition_filters[j];
        condition_filters[j] = nullptr;
      }
      delete[] condition_filters;
      condition_filters = nullptr;
      return rc;
    }
    condition_filters[i] = default_condition_filter;
  }
  return init((const ConditionFilter **)condition_filters, condition_num, true);
}

bool CompositeConditionFilter::filter(const Record &rec) const
{
  for (int i = 0; i < filter_num_; i++) {
    if (!filters_[i]->filter(rec)) {
      return false;
    }
  }
  return true;
}

DefaultInnerJoinFilter::~DefaultInnerJoinFilter() = default;

RC DefaultInnerJoinFilter::init(const JoinConDesc &left, const JoinConDesc &right, CompOp comp_op)
{
  if (comp_op < EQUAL_TO || comp_op >= NO_OP) {
    LOG_ERROR("Invalid condition with unsupported compare operation: %d", comp_op);
    return RC::INVALID_ARGUMENT;
  }

  left_ = left;
  right_ = right;
  comp_op_ = comp_op;
  return RC::SUCCESS;
}


bool DefaultInnerJoinFilter::filter(std::vector<Tuple> *tuples) const {
  std::shared_ptr<TupleValue> left_value = (*tuples)[left_.table_index].get_pointer(left_.value_index);
  std::shared_ptr<TupleValue> right_value = (*tuples)[right_.table_index].get_pointer(right_.value_index);
  if (left_value->Type() == AttrType::UNDEFINED || right_value->Type() == AttrType::UNDEFINED) {
    return false;
  }
  int cmp_result = left_value->compare(*right_value);
  return compare_result(cmp_result, comp_op_);
}

CompositeJoinFilter::~CompositeJoinFilter() {
  if (memory_owner_) {
    for (auto & filter : filters_) {
      delete filter;
    }
    std::vector<DefaultInnerJoinFilter *>().swap(filters_);
  }
}

RC CompositeJoinFilter::init(std::vector<DefaultInnerJoinFilter *> &&filters, bool own_memory) {
  filters_ = std::move(filters);
  memory_owner_ = own_memory;
  return RC::SUCCESS;
}

bool CompositeJoinFilter::filter(std::vector<Tuple> *tuples) const {
  if (tuples->empty()) {
    return false;
  }
  for (const auto & filter : filters_) {
    if (!filter->filter(tuples)) {
      return false;
    }
  }
  return true;
}
