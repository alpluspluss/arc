/* this project is part of the Arc project; licensed under the MIT license. see LICENSE for more info */

#pragma once

#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <arc/analysis/tbaa.hpp>
#include <arc/foundation/pass.hpp>
#include <arc/foundation/typed-data.hpp>

namespace arc
{
    class Module;
    class PassManager;
    struct Node;
    class Region;

    /**
     * @brief Information about a field access within a struct
     */
    struct FieldAccess
    {
        /** @brief The ACCESS node performing the field access */
        Node* access_node = nullptr;
        /** @brief Intermediate ACCESS node itself */
        Node* access_intermediate = nullptr;
        /** @brief Logical field index (excluding padding fields) */
        std::size_t field_index = 0;
        /** @brief True if this is a store operation, false for load */
        bool is_store = false;
    };

    /**
     * @brief Information collected about a promotable struct allocation
     */
    struct AllocationInfo
    {
        /** @brief The original ALLOC node */
        Node* alloc_node = nullptr;
        /** @brief Struct type being allocated */
        DataType struct_type = DataType::VOID;
        /** @brief Scalar allocations created for promoted fields (indexed by logical field index) */
        std::vector<Node*> scalar_allocs;
        /** @brief Set of field indices that have escaped and cannot be promoted */
        std::unordered_set<std::size_t> escaped_fields;
        /** @brief All field accesses found for this allocation */
        std::vector<FieldAccess> field_accesses;
        /** @brief True if all fields can be promoted to scalars */
        bool fully_promotable = true;
    };

    /**
     * @brief Scalar Replacement of Aggregates optimization pass
     *
     * Promotes struct allocations to individual scalar allocations when safe and profitable.
     * Supports partial promotion where only non-escaped fields are promoted while keeping
     * escaped fields in a reduced struct type.
     *
     * The pass uses TBAA for escape analysis and leverages Arc's consistent ACCESS[container, selector]
     * operand convention for clean field access detection.
     */
    class SROAPass final : public TransformPass
    {
    public:
        /**
         * @brief Get the pass name
         * @return Pass identifier for dependency resolution
         */
        [[nodiscard]] std::string name() const override;

        /**
         * @brief Get required analysis passes
         * @return Vector of analysis pass names needed by SROA
         */
        [[nodiscard]] std::vector<std::string> require() const override;

        /**
         * @brief Get analyses invalidated by this pass
         * @return Vector of analysis names that become stale after SROA
         */
        [[nodiscard]] std::vector<std::string> invalidates() const override;

        /**
         * @brief Run SROA optimization on the module
         * @param module Module to optimize
         * @param pm Pass manager for accessing cached analyses
         * @return Vector of regions that were modified
         */
        std::vector<Region*> run(Module& module, PassManager& pm) override;

    private:
        /**
         * @brief Process a single function for SROA opportunities
         * @param func_region Function's root region
         * @param tbaa TBAA analysis result for safety checks
         * @return Vector of regions modified in this function
         */
        static std::vector<Region*> process_function(Region* func_region, const TypeBasedAliasResult& tbaa);
    };
}
