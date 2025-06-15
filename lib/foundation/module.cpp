/* this project is part of the Arc project; licensed under the MIT license. see LICENSE for more info */

#include <arc/foundation/module.hpp>
#include <arc/foundation/region.hpp>

namespace arc
{
	Module::Module(std::string_view name)
	{
		regions.push_back(std::make_unique<Region>(".__global", *this, nullptr));
		root_region = regions.back().get();
		regions.push_back(std::make_unique<Region>(".__rodata", *this, nullptr));
		rodata_region = regions.back().get();
		mod_id = strtb.intern(name);
	}

	Module::~Module() = default;

	std::string_view Module::name() const
	{
		return strtb.get(mod_id);
	}

	Region *Module::root() const
	{
		return root_region;
	}

	Region *Module::create(std::string_view name, Region *parent)
	{
		if (!parent)
			parent = root_region;

		regions.push_back(std::make_unique<Region>(name, *this, parent));
		auto* ptr = regions.back().get();
		parent->add_child(ptr);
		return ptr;
	}

	Node *Module::find_fn(std::string_view name)
	{
		const auto name_id = strtb.intern(name);
		for (Node* fn : fns)
		{
			if (fn->str_id == name_id)
				return fn;
		}
		return nullptr;
	}

	void Module::add_fn(Node *fn)
	{
		if (!fn || fn->ir_type != NodeType::FUNCTION)
			return;

		if (std::ranges::find(fns, fn) == fns.end())
			fns.push_back(fn);
	}

	void Module::add_rodata(Node *node)
	{
		if (!node)
			return;

		rodata_region->append(node);
	}

	StringTable::StringId Module::intern_str(std::string_view str)
	{
		return strtb.intern(str);
	}

	bool Module::contains(Node* fn)
	{
		if (!fn)
			return false;
		return std::ranges::find(fns, fn) != fns.end();
	}

	bool Module::contains(Region* region)
	{
		if (!region)
			return false;

		for (const auto& r : regions)
		{
			if (r.get() == region)
				return true;
		}

		return false;
	}

	const std::vector<Node*>& Module::functions() const
	{
		return fns;
	}

	Region* Module::rodata() const
	{
		return rodata_region;
	}
}
