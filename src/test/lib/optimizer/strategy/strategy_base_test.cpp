#include "strategy_base_test.hpp"

#include <memory>
#include <string>
#include <utility>

#include "cost_estimation/cost_estimator_logical.hpp"
#include "logical_query_plan/abstract_lqp_node.hpp"
#include "logical_query_plan/logical_plan_root_node.hpp"
#include "optimizer/optimizer.hpp"
#include "optimizer/strategy/abstract_rule.hpp"
#include "statistics/cardinality_estimator.hpp"

namespace hyrise {

std::shared_ptr<AbstractLQPNode> StrategyBaseTest::apply_rule(const std::shared_ptr<AbstractRule>& rule,
                                                              const std::shared_ptr<AbstractLQPNode>& input) {
  const auto lqp_result = apply_rule_with_cache(rule, input);
  const auto lqp = lqp_result.logical_query_plan;

  return lqp;
}

OptimizedLogicalQueryPlan StrategyBaseTest::apply_rule_with_cache(const std::shared_ptr<AbstractRule>& rule,
                                                                  const std::shared_ptr<AbstractLQPNode>& input) {
  // Add explicit root node
  const auto root_node = LogicalPlanRootNode::make();
  root_node->set_left_input(input);

  // Create estimators
  const auto cardinality_estimator = std::make_shared<CardinalityEstimator>();
  const auto cost_estimator = std::make_shared<CostEstimatorLogical>(cardinality_estimator);
  rule->cost_estimator = cost_estimator;

  const auto cacheable = rule->apply_to_plan(root_node);

  // Remove LogicalPlanRootNode
  const auto optimized_node = root_node->left_input();
  root_node->set_left_input(nullptr);

  return {static_cast<bool>(cacheable), optimized_node};
}

}  // namespace hyrise
