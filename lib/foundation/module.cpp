/* this project is part of the Arc project; licensed under the MIT license. see LICENSE for more info */

#include <arc/foundation/module.hpp>
#include <arc/foundation/region.hpp>

namespace arc
{
	Module::Module(std::string_view name)
	{
		auto* root_mem = region_alloc.allocate(1);
		root_region = std::construct_at(root_mem, ".__global", *this, nullptr);
		regions.push_back(root_region);

		auto* rodata_mem = region_alloc.allocate(1);
		rodata_region = std::construct_at(rodata_mem, ".__rodata", *this, nullptr);
		regions.push_back(rodata_region);
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

	Region *Module::create_region(std::string_view name, Region *parent)
	{
		if (!parent)
			parent = root_region;

		auto* mem = region_alloc.allocate(1);
		auto* region = std::construct_at(mem, name, *this, parent);
		regions.push_back(region);
		parent->add_child(region);
		return region;
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
		return std::ranges::find(regions, region) != regions.end();
	}

	const std::vector<Node*>& Module::functions() const
	{
		return fns;
	}

	Region* Module::rodata() const
	{
		return rodata_region;
	}

	StringTable &Module::strtable()
	{
		return strtb;
	}

	const TypedData &Module::add_t(const std::string& name, TypedData tdef)
	{
		if (typedefs.contains(name))
			throw std::runtime_error("type '" + name + "' already defined");

		auto [it, inserted] = typedefs.emplace(name, std::move(tdef));
		return it->second;
	}

	TypedData& Module::at_t(std::string_view name)
	{
		const auto it = typedefs.find(std::string(name));
		if (it == typedefs.end())
			throw std::runtime_error("type '" + std::string(name) + "' not found");
		return it->second;
	}

	const std::unordered_map<std::string, TypedData> & Module::typemap()
	{
		return typedefs;
	}
}
