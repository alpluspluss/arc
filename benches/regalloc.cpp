/* this project is part of the Arc project; licensed under the MIT license. see LICENSE for more info */

#include <chrono>
#include <random>
#include <arc/codegen/regalloc.hpp>
#include <arc/foundation/builder.hpp>
#include <arc/foundation/module.hpp>
#include <benchmark/benchmark.h>

struct MobileTarget
{
    using register_type = std::uint32_t;
    struct MobileInstruction
    {
        enum class Opcode { ADD, SUB, MUL, MOV, LDR, STR, VADD, VSUB, VMUL, CMP, B, BL };
        static constexpr std::size_t max_operands() { return 3; }
        static constexpr std::size_t encoding_size() { return 4; }
    };

    using instruction_type = MobileInstruction;
    static constexpr arc::TargetArch target_arch() { return arc::TargetArch::AARCH64; }

    std::uint32_t count(arc::RegisterClass cls) const
    {
        switch (cls) {
            case arc::RegisterClass::GENERAL_PURPOSE: return 13;
            case arc::RegisterClass::VECTOR: return 16;
            case arc::RegisterClass::PREDICATE: return 0;
        }
        return 0;
    }

    std::vector<register_type> caller_saved(arc::RegisterClass cls) const
    {
        if (cls == arc::RegisterClass::GENERAL_PURPOSE)
            return {0, 1, 2, 3, 12};
        if (cls == arc::RegisterClass::VECTOR)
            return {0, 1, 2, 3, 4, 5, 6, 7};
        return {};
    }

    std::vector<register_type> callee_saved(arc::RegisterClass cls) const
    {
        if (cls == arc::RegisterClass::GENERAL_PURPOSE)
            return {4, 5, 6, 7, 8, 9, 10, 11};
        if (cls == arc::RegisterClass::VECTOR)
            return {8, 9, 10, 11, 12, 13, 14, 15};
        return {};
    }

    std::uint32_t spill_cost(register_type reg) const
    {
        return reg < 13 ? 6 : 12;
    }

    bool uses_vector_for_float() const { return true; }
};

class MobileBenchmark
{
public:
    MobileBenchmark() : target(), rng(42) {}

    struct BenchResult
    {
        std::uint32_t gp_allocated = 0;
        std::uint32_t vec_allocated = 0;
        std::uint32_t gp_spilled = 0;
        std::uint32_t vec_spilled = 0;
        std::uint32_t from_nodes_optimized = 0;
        double allocation_time_ms = 0.0;
        double gp_pressure_ratio = 0.0;
        double vec_pressure_ratio = 0.0;
        double spill_ratio = 0.0;
    };

    BenchResult bench_computation_chain(std::uint32_t chain_length, bool use_floats)
    {
        auto module = std::make_unique<arc::Module>("mobile_bench");
        auto region = module->create_region("compute_region");
        arc::Builder builder(*module);
        builder.set_insertion_point(region);

        std::vector<arc::Node*> computations;

        if (use_floats) {
            auto* base1 = builder.lit(1.5f);
            auto* base2 = builder.lit(2.5f);
            arc::Node* result = builder.add(base1, base2);
            computations.push_back(result);

            for (std::uint32_t i = 1; i < chain_length; ++i) {
                auto* operand = builder.lit(static_cast<float>(i) * 0.1f);
                result = (i % 3 == 0) ? builder.mul(result, operand) : builder.add(result, operand);
                computations.push_back(result);
            }
        } else {
            auto* base1 = builder.lit(10);
            auto* base2 = builder.lit(20);
            arc::Node* result = builder.add(base1, base2);
            computations.push_back(result);

            for (std::uint32_t i = 1; i < chain_length; ++i) {
                auto* operand = builder.lit(static_cast<std::int32_t>(i + 5));
                result = (i % 3 == 0) ? builder.mul(result, operand) : builder.add(result, operand);
                computations.push_back(result);
            }
        }

        auto dag = std::make_unique<arc::SelectionDAG<MobileTarget::instruction_type>>(region);
        dag->build();

        arc::RegisterAllocator<MobileTarget> allocator(target, *dag);
        auto budget = create_mobile_budget();

        auto start = std::chrono::high_resolution_clock::now();
        allocator.allocate(region, budget);
        auto end = std::chrono::high_resolution_clock::now();

        BenchResult result;
        result.allocation_time_ms = std::chrono::duration<double, std::milli>(end - start).count();

        arc::RegisterClass target_class = use_floats ?
            arc::RegisterClass::VECTOR : arc::RegisterClass::GENERAL_PURPOSE;

        for (auto* ir_node : computations) {
            auto* dag_node = dag->find(ir_node);
            if (dag_node && dag_node->kind == arc::NodeKind::VALUE && dag_node->value_t != arc::DataType::VOID) {
                arc::Request<MobileTarget> req;
                req.cls = target_class;
                auto alloc_result = allocator.allocate_node(dag_node, req);

                if (alloc_result.allocated()) {
                    if (use_floats) result.vec_allocated++;
                    else result.gp_allocated++;
                } else if (alloc_result.spilled) {
                    if (use_floats) result.vec_spilled++;
                    else result.gp_spilled++;
                }
            }
        }

        result.gp_pressure_ratio = static_cast<double>(allocator.pressure(region, arc::RegisterClass::GENERAL_PURPOSE)) / 13.0;
        result.vec_pressure_ratio = static_cast<double>(allocator.pressure(region, arc::RegisterClass::VECTOR)) / 16.0;

        std::uint32_t total_values = result.gp_allocated + result.vec_allocated + result.gp_spilled + result.vec_spilled;
        result.spill_ratio = total_values > 0 ?
            static_cast<double>(result.gp_spilled + result.vec_spilled) / total_values : 0.0;

        return result;
    }

    BenchResult bench_parallel_computations(std::uint32_t int_chains, std::uint32_t float_chains)
    {
        auto module = std::make_unique<arc::Module>("parallel_bench");
        auto region = module->create_region("parallel_region");
        arc::Builder builder(*module);
        builder.set_insertion_point(region);

        std::vector<arc::Node*> all_computations;

        for (std::uint32_t chain = 0; chain < int_chains; ++chain) {
            auto* base = builder.lit(static_cast<std::int32_t>(chain * 10));
            auto* increment = builder.lit(1);
            auto* result = builder.add(base, increment);
            all_computations.push_back(result);

            for (int step = 0; step < 3; ++step) {
                auto* next_increment = builder.lit(static_cast<std::int32_t>(step + 2));
                result = builder.mul(result, next_increment);
                all_computations.push_back(result);
            }
        }

        for (std::uint32_t chain = 0; chain < float_chains; ++chain) {
            auto* base = builder.lit(static_cast<float>(chain) * 0.5f);
            auto* increment = builder.lit(0.1f);
            auto* result = builder.add(base, increment);
            all_computations.push_back(result);

            for (int step = 0; step < 3; ++step) {
                auto* next_increment = builder.lit(static_cast<float>(step + 1) * 0.2f);
                result = builder.mul(result, next_increment);
                all_computations.push_back(result);
            }
        }

        auto dag = std::make_unique<arc::SelectionDAG<MobileTarget::instruction_type>>(region);
        dag->build();

        arc::RegisterAllocator<MobileTarget> allocator(target, *dag);
        auto budget = create_mobile_budget();

        auto start = std::chrono::high_resolution_clock::now();
        allocator.allocate(region, budget);
        auto end = std::chrono::high_resolution_clock::now();

        BenchResult result;
        result.allocation_time_ms = std::chrono::duration<double, std::milli>(end - start).count();

        for (auto* ir_node : all_computations) {
            auto* dag_node = dag->find(ir_node);
            if (dag_node && dag_node->kind == arc::NodeKind::VALUE && dag_node->value_t != arc::DataType::VOID) {
                bool is_float = (ir_node->type_kind == arc::DataType::FLOAT32);
                arc::RegisterClass expected_class = is_float ?
                    arc::RegisterClass::VECTOR : arc::RegisterClass::GENERAL_PURPOSE;

                arc::Request<MobileTarget> req;
                req.cls = expected_class;
                auto alloc_result = allocator.allocate_node(dag_node, req);

                if (alloc_result.allocated()) {
                    if (is_float) result.vec_allocated++;
                    else result.gp_allocated++;
                } else if (alloc_result.spilled) {
                    if (is_float) result.vec_spilled++;
                    else result.gp_spilled++;
                }
            }
        }

        result.gp_pressure_ratio = static_cast<double>(allocator.pressure(region, arc::RegisterClass::GENERAL_PURPOSE)) / 13.0;
        result.vec_pressure_ratio = static_cast<double>(allocator.pressure(region, arc::RegisterClass::VECTOR)) / 16.0;

        std::uint32_t total_values = result.gp_allocated + result.vec_allocated + result.gp_spilled + result.vec_spilled;
        result.spill_ratio = total_values > 0 ?
            static_cast<double>(result.gp_spilled + result.vec_spilled) / total_values : 0.0;

        return result;
    }

    BenchResult bench_control_flow_merge()
    {
        auto module = std::make_unique<arc::Module>("control_flow_bench");
        auto main_region = module->create_region("main");
        auto branch1 = module->create_region("then_branch", main_region);
        auto branch2 = module->create_region("else_branch", main_region);
        auto merge = module->create_region("merge", main_region);

        arc::Builder builder(*module);

        builder.set_insertion_point(branch1);
        auto* then_base = builder.lit(42);
        auto* then_increment = builder.lit(10);
        auto* then_result = builder.add(then_base, then_increment);

        builder.set_insertion_point(branch2);
        auto* else_base = builder.lit(24);
        auto* else_multiplier = builder.lit(2);
        auto* else_result = builder.mul(else_base, else_multiplier);

        builder.set_insertion_point(merge);
        auto* from_node = builder.create_node(arc::NodeType::FROM, arc::DataType::INT32);
        from_node->inputs = {then_result, else_result};

        auto* final_increment = builder.lit(5);
        auto* final_result = builder.add(from_node, final_increment);

        auto dag = std::make_unique<arc::SelectionDAG<MobileTarget::instruction_type>>(merge);
        dag->build();

        arc::RegisterAllocator<MobileTarget> allocator(target, *dag);
        auto budget = create_mobile_budget();

        auto start = std::chrono::high_resolution_clock::now();
        allocator.allocate(merge, budget);
        auto end = std::chrono::high_resolution_clock::now();

        BenchResult result;
        result.allocation_time_ms = std::chrono::duration<double, std::milli>(end - start).count();

        auto* dag_from = dag->find(from_node);
        auto* dag_final = dag->find(final_result);

        if (dag_from && dag_final) {
            arc::Request<MobileTarget> req;
            req.cls = arc::RegisterClass::GENERAL_PURPOSE;

            auto from_result = allocator.allocate_node(dag_from, req);
            auto final_alloc = allocator.allocate_node(dag_final, req);

            if (from_result.allocated()) {
                result.gp_allocated++;
                bool used_caller_saved = is_caller_saved(*from_result.reg);
                if (used_caller_saved) result.from_nodes_optimized++;
            }
            if (final_alloc.allocated()) result.gp_allocated++;
            if (from_result.spilled) result.gp_spilled++;
            if (final_alloc.spilled) result.gp_spilled++;
        }

        result.gp_pressure_ratio = static_cast<double>(allocator.pressure(merge, arc::RegisterClass::GENERAL_PURPOSE)) / 13.0;

        return result;
    }

private:
    MobileTarget target;
    std::mt19937 rng;

    arc::Budget<MobileTarget> create_mobile_budget()
    {
        arc::Budget<MobileTarget> budget;

        auto gp_caller = target.caller_saved(arc::RegisterClass::GENERAL_PURPOSE);
        auto gp_callee = target.callee_saved(arc::RegisterClass::GENERAL_PURPOSE);
        auto vec_caller = target.caller_saved(arc::RegisterClass::VECTOR);
        auto vec_callee = target.callee_saved(arc::RegisterClass::VECTOR);

        for (auto reg : gp_caller) {
            budget.available[arc::RegisterClass::GENERAL_PURPOSE].insert(reg);
        }
        for (auto reg : gp_callee) {
            budget.available[arc::RegisterClass::GENERAL_PURPOSE].insert(reg);
        }
        for (auto reg : vec_caller) {
            budget.available[arc::RegisterClass::VECTOR].insert(reg);
        }
        for (auto reg : vec_callee) {
            budget.available[arc::RegisterClass::VECTOR].insert(reg);
        }

        return budget;
    }

    bool is_caller_saved(std::uint32_t reg) const
    {
        auto caller_saved = target.caller_saved(arc::RegisterClass::GENERAL_PURPOSE);
        return std::find(caller_saved.begin(), caller_saved.end(), reg) != caller_saved.end();
    }
};

static void BM_IntegerChain(benchmark::State& state)
{
    MobileBenchmark bench;
    std::uint32_t chain_length = state.range(0);

    for (auto _ : state) {
        auto result = bench.bench_computation_chain(chain_length, false);
        state.counters["gp_allocated"] = result.gp_allocated;
        state.counters["gp_spilled"] = result.gp_spilled;
        state.counters["gp_pressure"] = result.gp_pressure_ratio;
        state.counters["spill_ratio"] = result.spill_ratio;
        state.counters["alloc_time_ms"] = result.allocation_time_ms;
    }
}

static void BM_FloatChain(benchmark::State& state)
{
    MobileBenchmark bench;
    std::uint32_t chain_length = state.range(0);

    for (auto _ : state) {
        auto result = bench.bench_computation_chain(chain_length, true);
        state.counters["vec_allocated"] = result.vec_allocated;
        state.counters["vec_spilled"] = result.vec_spilled;
        state.counters["vec_pressure"] = result.vec_pressure_ratio;
        state.counters["spill_ratio"] = result.spill_ratio;
        state.counters["alloc_time_ms"] = result.allocation_time_ms;
    }
}

static void BM_ParallelComputations(benchmark::State& state)
{
    MobileBenchmark bench;
    std::uint32_t int_chains = state.range(0);
    std::uint32_t float_chains = state.range(1);

    for (auto _ : state) {
        auto result = bench.bench_parallel_computations(int_chains, float_chains);
        state.counters["gp_allocated"] = result.gp_allocated;
        state.counters["vec_allocated"] = result.vec_allocated;
        state.counters["gp_spilled"] = result.gp_spilled;
        state.counters["vec_spilled"] = result.vec_spilled;
        state.counters["gp_pressure"] = result.gp_pressure_ratio;
        state.counters["vec_pressure"] = result.vec_pressure_ratio;
        state.counters["spill_ratio"] = result.spill_ratio;
        state.counters["alloc_time_ms"] = result.allocation_time_ms;
    }
}

static void BM_ControlFlowMerge(benchmark::State& state)
{
    MobileBenchmark bench;

    for (auto _ : state) {
        auto result = bench.bench_control_flow_merge();
        state.counters["gp_allocated"] = result.gp_allocated;
        state.counters["from_optimized"] = result.from_nodes_optimized;
        state.counters["gp_pressure"] = result.gp_pressure_ratio;
        state.counters["alloc_time_ms"] = result.allocation_time_ms;
    }
}

BENCHMARK(BM_IntegerChain)->Range(8, 20)->Unit(benchmark::kMicrosecond);
BENCHMARK(BM_FloatChain)->Range(8, 24)->Unit(benchmark::kMicrosecond);
BENCHMARK(BM_ParallelComputations)->Ranges({{2, 6}, {2, 8}})->Unit(benchmark::kMicrosecond);
BENCHMARK(BM_ControlFlowMerge)->Unit(benchmark::kMicrosecond);

BENCHMARK_MAIN();
