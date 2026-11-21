#pragma once

#include <boost/test/unit_test.hpp>

#include "miaow.hpp"
#include "miaow_io.hpp"

#include <memory>
#include <utility>
#include <vector>

namespace miaow::tests {

// Observe controller results without changing the workload's submission policy
// The checker must outlive the runtime; inspect results only after get() returns
class WorkflowRecordChecker {
  public:
    std::unique_ptr<miaow::WorkflowController>
    observe(std::unique_ptr<miaow::WorkflowController> controller) {
        return std::make_unique<Controller>(*this, std::move(controller));
    }

    // Verify that finalization completed and its output reached the workflow record
    void check(const miaow::WorkflowRecord& record) const {
        BOOST_TEST(finalized_);
        BOOST_TEST(miaow::to_json(record.output_) == miaow::to_json(output_));
    }

    std::vector<std::pair<miaow::WorkflowChildId, miaow::JobResult>> jobs;
    std::vector<std::pair<miaow::WorkflowChildId, miaow::WorkflowResult>> workflows;

  private:
    class Controller final : public miaow::WorkflowController {
      public:
        Controller(WorkflowRecordChecker& checker,
                   std::unique_ptr<miaow::WorkflowController> controller)
            : checker_{checker}, controller_{std::move(controller)} {}

        void initialize(miaow::Workflow& workflow) override {
            controller_->initialize(workflow);
        }

        void callback(miaow::Workflow& workflow, miaow::WorkflowChildId child_id,
                      const miaow::JobResult& result) override {
            controller_->callback(workflow, child_id, result);
            checker_.jobs.emplace_back(child_id, result);
        }

        void callback(miaow::Workflow& workflow, miaow::WorkflowChildId child_id,
                      const miaow::WorkflowResult& result) override {
            controller_->callback(workflow, child_id, result);
            checker_.workflows.emplace_back(child_id, result);
        }

        caf::config_value finalize() override {
            checker_.output_ = controller_->finalize();
            checker_.finalized_ = true;
            return checker_.output_;
        }

      private:
        WorkflowRecordChecker& checker_;
        std::unique_ptr<miaow::WorkflowController> controller_;
    };

    bool finalized_{false};
    caf::config_value output_;
};

} // namespace miaow::tests
