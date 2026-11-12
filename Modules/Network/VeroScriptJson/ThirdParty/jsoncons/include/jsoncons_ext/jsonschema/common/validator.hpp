// Copyright 2013-2026 Daniel Parker
// Distributed under the Boost license, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)

// See https://github.com/danielaparker/jsoncons for latest version

#ifndef JSONCONS_EXT_JSONSCHEMA_COMMON_VALIDATOR_HPP
#define JSONCONS_EXT_JSONSCHEMA_COMMON_VALIDATOR_HPP

#include <cstddef>
#include <algorithm>
#include <functional>
#include <string>
#include <unordered_set>
#include <vector>

#include <jsoncons/config/jsoncons_config.hpp>
#include <jsoncons/utility/uri.hpp>
#include <jsoncons_ext/jsonschema/common/eval_context.hpp>
#include <jsoncons_ext/jsonschema/jsonschema_error.hpp>
#include <jsoncons_ext/jsonschema/validation_message.hpp>
#include <jsoncons_ext/jsonpointer/jsonpointer.hpp>

namespace jsoncons {
namespace jsonschema {
    
    enum class walk_state {advance, abort};

    // For backwards compatibility
    using walk_result = walk_state;

    // Interface for validation error handlers
    template <typename Json>
    class error_reporter
    {
        std::size_t error_count_{0};
    public:
        error_reporter() = default;

        virtual ~error_reporter() = default;

        walk_state error(const validation_message& msg, jsoncons::optional<Json>& patch)
        {
#if defined(VEROSCRIPT_JSON_SCHEMA_POLICY)
            VEROSCRIPT_JSON_SCHEMA_POLICY::Report(msg.message().size());
#endif
            ++error_count_;
            return do_error(msg, patch);
        }

        std::size_t error_count() const
        {
            return error_count_;
        }

    private:
        virtual walk_state do_error(const validation_message& /* msg */, 
            jsoncons::optional<Json>& /*patch*/) = 0;
    };

    template <typename Json>
    struct collecting_error_listener : public error_reporter<Json>
    {
        std::vector<validation_message> errors;

    private:
        walk_state do_error(const validation_message& msg, 
            jsoncons::optional<Json>& /*patch*/) final
        {
            errors.push_back(msg);
            return walk_state::advance;
        }
    };

    template <typename Json>
    class schema_property 
    {
    public:
        virtual ~schema_property() = default;
        virtual const std::string& keyword() const = 0; 
        virtual const Json& subschemas() const = 0;
        virtual const uri& schema_location() const = 0; 
    };

    // Interface for validation error handlers
    template <typename Json>
    class walk_reporter
    {
    public:
        walk_reporter() = default;

        virtual ~walk_reporter() = default;

        walk_state walk(const schema_property<Json>& property,
            const Json& instance, const jsonpointer::json_pointer& instance_location,
            jsoncons::optional<Json>& patch) 
        {
            return do_walk(property, instance, instance_location, patch);
        }

    private:
        virtual walk_state do_walk(const schema_property<Json>& property,
            const Json& instance, const jsonpointer::json_pointer& instance_location,
            jsoncons::optional<Json>& patch) = 0;
    };

    class range
    {
        std::size_t start_{0};
        std::size_t end_{0};
    public:
        range()
        {
        }
    
        range(std::size_t start, std::size_t end)
            : start_(start), end_(end)
        {
        }
        
        std::size_t start() const
        {
            return start_;
        }
    
        std::size_t end() const
        {
            return end_;
        }
        
        bool contains(std::size_t index) const
        {
            return index >= start_ && index < end_; 
        }
    };
    
    class range_collection
    {
        std::vector<range> ranges_;
    public:    
        using const_iterator = std::vector<range>::const_iterator;
        using value_type = range;
        
        range_collection()
        {
        }
        range_collection(const range_collection& other) = default;
        range_collection(range_collection&& other) = default;

        range_collection& operator=(const range_collection& other) = default;
        range_collection& operator=(range_collection&& other) = default;
        
        std::size_t size() const
        {
            return ranges_.size();
        }
    
        range operator[](std::size_t index) const
        {
            return ranges_[index];
        }
        
        const_iterator begin() const
        {
            return ranges_.cbegin();
        }

        const_iterator end() const
        {
            return ranges_.cend();
        }
        
        void insert(range index_range)
        {
#if defined(VEROSCRIPT_JSON_SCHEMA_POLICY)
            VEROSCRIPT_JSON_SCHEMA_POLICY::Check();
            if (index_range.start() >= index_range.end()) return;
            if (ranges_.empty() || ranges_.back().end() < index_range.start())
            {
                ranges_.push_back(index_range);
                return;
            }
            if (ranges_.back().start() <= index_range.start())
            {
                ranges_.back() = range(ranges_.back().start(), (std::max)(ranges_.back().end(), index_range.end()));
                return;
            }
            auto first = std::lower_bound(ranges_.begin(), ranges_.end(), index_range.start(),
                [](const range& item, std::size_t value) { return item.end() < value; });
            auto last = first;
            auto begin = index_range.start();
            auto end = index_range.end();
            while (last != ranges_.end() && last->start() <= end)
            {
                VEROSCRIPT_JSON_SCHEMA_POLICY::Check();
                begin = (std::min)(begin, last->start());
                end = (std::max)(end, last->end());
                ++last;
            }
            VEROSCRIPT_JSON_SCHEMA_POLICY::Check(static_cast<std::size_t>(ranges_.end() - first) + 1);
            if (first == last) ranges_.insert(first, range(begin, end));
            else
            {
                *first = range(begin, end);
                ranges_.erase(first + 1, last);
            }
#else
            ranges_.push_back(index_range);
#endif
        }
    
        bool contains(std::size_t index) const
        {
#if defined(VEROSCRIPT_JSON_SCHEMA_POLICY)
            const auto item = std::lower_bound(ranges_.begin(), ranges_.end(), index,
                [](const range& value, std::size_t position)
                {
                    VEROSCRIPT_JSON_SCHEMA_POLICY::Check();
                    return value.end() <= position;
                });
            return item != ranges_.end() && item->contains(index);
#else
            bool found = false;
            std::size_t length = ranges_.size();
            for (std::size_t i = 0; i < length && !found; ++i)
            {
                if (ranges_[i].contains(index))
                {
                    found = true;
                }
            }
            return found;
#endif
        }
    };
    
    struct evaluation_results
    {
        std::unordered_set<std::string> evaluated_properties;
        range_collection evaluated_items;

        void merge(const evaluation_results& results)
        {
            for (auto&& name : results.evaluated_properties)
            {
                evaluated_properties.insert(name);
            }
            for (auto index_range : results.evaluated_items)
            {
                evaluated_items.insert(index_range);
            }
        }
        void merge(std::unordered_set<std::string>&& properties)
        {
            auto end = std::make_move_iterator(properties.end());
            for (auto it = std::make_move_iterator(properties.begin()); it != end; ++it)
            {
                evaluated_properties.insert(*it);
            }
        }
        void merge(const range_collection& ranges)
        {
            for (auto index_range : ranges)
            {
                evaluated_items.insert(index_range);
            }
        }
    };

    template <typename Json>
    class validator_base 
    {
    public:
        virtual ~validator_base() = default;

        virtual const uri& schema_location() const = 0;
#if defined(VEROSCRIPT_JSON_SCHEMA_POLICY)
        virtual std::string annotation_keyword() const { return {}; }
#endif

        walk_state validate(const eval_context<Json>& context,
            const Json& instance, 
            const jsonpointer::json_pointer& instance_location,
            evaluation_results& results, 
            error_reporter<Json>& reporter, 
            jsoncons::optional<Json>& patch) const 
        {
#if defined(VEROSCRIPT_JSON_SCHEMA_STEP)
            VEROSCRIPT_JSON_SCHEMA_STEP step;
#endif
#if defined(VEROSCRIPT_JSON_SCHEMA_POLICY)
            auto* collector = VEROSCRIPT_JSON_SCHEMA_POLICY::Annotations();
            VEROSCRIPT_JSON_SCHEMA_POLICY::FAnnotationScope annotations(collector);
            const auto errors = reporter.error_count();
            if (collector && VEROSCRIPT_JSON_SCHEMA_POLICY::EvaluationKeyword(annotation_keyword()))
            {
                const auto keyword = annotation_keyword();
                const bool unevaluated = keyword == "unevaluatedProperties" || keyword == "unevaluatedItems";
                evaluation_results local;
                if (unevaluated) local = results;
                const auto state = do_validate(context, instance, instance_location, local, reporter, patch);
                if (state == walk_state::advance && errors == reporter.error_count())
                {
                    VEROSCRIPT_JSON_SCHEMA_POLICY::Evaluated(keyword, schema_location().string(),
                        context.eval_path().string(), instance_location.string(), instance, local,
                        unevaluated ? &results : nullptr);
                    annotations.Commit();
                }
                if (unevaluated) results = std::move(local);
                else results.merge(local);
                return state;
            }
            const auto state = do_validate(context, instance, instance_location, results, reporter, patch);
            if (state == walk_state::advance && errors == reporter.error_count()) annotations.Commit();
            return state;
#else
            return do_validate(context, instance, instance_location, results, reporter, patch);
#endif
        }

        walk_state walk(const eval_context<Json>& context, const Json& instance, 
            const jsonpointer::json_pointer& instance_location, walk_reporter<Json>& reporter, jsoncons::optional<Json>& patch) const
        {
#if defined(VEROSCRIPT_JSON_SCHEMA_STEP)
            VEROSCRIPT_JSON_SCHEMA_STEP step;
#endif
            return do_walk(context, instance, instance_location, reporter, patch);
        }
        
        virtual bool always_fails() const = 0;

        virtual bool always_succeeds() const = 0;


    private:
        virtual walk_state do_validate(const eval_context<Json>& context, const Json& instance, 
            const jsonpointer::json_pointer& instance_location,
            evaluation_results& results, 
            error_reporter<Json>& reporter, 
            jsoncons::optional<Json>& patch) const = 0;

        virtual walk_state do_walk(const eval_context<Json>& /*context*/, const Json& instance, 
            const jsonpointer::json_pointer& instance_location, walk_reporter<Json>& reporter, 
            jsoncons::optional<Json>& patch) const = 0;
   };

    class validation_message_factory
    {
    public:
        virtual ~validation_message_factory() = default;
        
        virtual validation_message make_validation_message(const jsonpointer::json_pointer& eval_path,
            const jsonpointer::json_pointer& instance_location,
            const std::string& message) const = 0;

        virtual validation_message make_validation_message(const jsonpointer::json_pointer& eval_path,
            const jsonpointer::json_pointer& instance_location,
            const std::string& message,
            const std::vector<validation_message>& details) const = 0;
    };

} // namespace jsonschema
} // namespace jsoncons

#endif // JSONCONS_EXT_JSONSCHEMA_KEYWORD_VALIDATOR_HPP
