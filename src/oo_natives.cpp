// 
//		OO (Object-Oriention) support as a module enabled for AMX Mod X plugins.
//		Copyright (C) 2022  Hon Fai
// 
//		This program is free software: you can redistribute itand /or modify 
//		it under the terms of the GNU General Public License as published by 
//		the Free Software Foundation, either version 3 of the License, or 
//		(at your option) any later version.
// 
//		This program is distributed in the hope that it will be useful, 
//		but WITHOUT ANY WARRANTY; without even the implied warranty of 
//		MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
//		GNU General Public License for more details.
// 
//		You should have received a copy of the GNU General Public License 
//		along with this program. If not, see <https://www.gnu.org/licenses/>.
// 

#include <utility>

#include <cmath>
#include <cstring>

#include "assembly_create.h"
#include "memory_.h"

#include "oo_natives.h"

#include "oo_defs.h"
#include "oo_class.h"
#include "oo_object.h"
#include "oo_manager.h"
#include "oo_utils.h"

namespace oo::natives
{
	int AddMethod(AMX *amx, const char* callback, const ArgList* args=nullptr)
	{
		AssemblyCreate assembly;
		assembly.add<Inst_Enter>();

		int num_args = (args == nullptr) ? 0 : args->size();
		int start = num_args - 1;
		int end = 0;

		assembly.add<Inst_Push_VAL>()->set_long(FP_DONE);

		for (int i = start; i >= end; i--)
		{
			cell type = 0;

			switch (args->at(i))
			{
			case OO_CELL:
				type = FP_CELL;
				break;

			case OO_BYREF:
				type = FP_CELL_BYREF;
				break;

			case OO_STRING:
				type = FP_STRING;
				break;

			case OO_STRING_EX:
				type = FP_STRINGEX;
				break;

			default:
				if (args->at(i) > OO_CELL) // ARRAY
					type = FP_ARRAY;
				
				break;
			}

			assembly.add<Inst_Push_VAL>()->set_long(type);
		}

		assembly.add<Inst_Push_VAL>()->set_long((long)callback);
		assembly.add<Inst_Push_VAL>()->set_long((long)amx);

		Inst_Call* inst_call = assembly.add<Inst_Call>();

		assembly.add<Inst_Add_ESP_Val>()->set_inc(4 * (num_args + 3)); // real + 3?
		assembly.add<Inst_Leave>();
		assembly.add<Inst_Ret>();

		int size = assembly.size();

		unsigned char* block = assembly.get_block();

		inst_call->set_address((long)MF_RegisterSPForwardByName);

		Memory m;
		m.make_writable_executable((long)block, size);

		return reinterpret_cast<long(*)()>(block)();
	}

	cell ExecuteMethod(AMX* amx, cell* params, int8_t num_params, int32_t forward_id, ObjectHash this_hash, const ArgList* args = nullptr, int8_t start_param = 0)
	{
		Manager::Instance()->PushThis(this_hash);

		int num_args = (args == nullptr) ? 0 : args->size();

		AssemblyCreate assembly;
		assembly.add<Inst_Enter>();

		std::vector<std::pair<std::string, int>> str_arr;
		str_arr.reserve(num_args);

		int start = num_args - 1;
		int end = 0;

		for (int i = start; i >= end; i--)
		{
			int p = i + start_param;
			int8_t type = args->at(i);

			switch (type)
			{
			case OO_CELL:
				assembly.add<Inst_Push_VAL>()->set_long(MF_GetAmxAddr(amx, params[p])[0]);
				break;

			case OO_BYREF:
				assembly.add<Inst_Push_VAL>()->set_long((long)&MF_GetAmxAddr(amx, params[p])[0]);
				break;

			case OO_STRING:
			case OO_STRING_EX:
			{
				int len = 0;
				str_arr.push_back(std::make_pair(MF_GetAmxString(amx, params[p], 0, &len), type == OO_STRING ? 0 : p));
				assembly.add<Inst_Push_VAL>()->set_long((long)(str_arr.back().first.data()));
				break;
			}

			default:
				if (type > OO_CELL)
					assembly.add<Inst_Push_VAL>()->set_long(MF_PrepareCellArrayA(MF_GetAmxAddr(amx, params[p]), type, true));

				break;
			}
		}

		assembly.add<Inst_Push_VAL>()->set_long((long)forward_id);

		Inst_Call* inst_call = assembly.add<Inst_Call>();

		assembly.add<Inst_Add_ESP_Val>()->set_inc(4 * (num_args + 1));
		assembly.add<Inst_Leave>();
		assembly.add<Inst_Ret>();

		int size = assembly.size();

		unsigned char* block = assembly.get_block();

		inst_call->set_address((long)MF_ExecuteForward);

		Memory m;
		m.make_writable_executable((long)block, size);

		cell ret = reinterpret_cast<long(*)()>(block)();

		// copyback string
		for (auto &e : str_arr)
		{
			int p = e.second;
			if (p > 0)
				MF_SetAmxString(amx, params[p], e.first.data(), strlen(e.first.data()));
		}

		Manager::Instance()->PopThis();

		return ret;
	}

	// native oo_decl_class(const class[], const base[] = "", const version_no = OO_VERSION_NUM);
	// e.g.
	// oo_decl_class("Base",	"");
	// oo_decl_class("Derived",	"Base");
	cell AMX_NATIVE_CALL native_decl_class(AMX *amx, cell params[])
	{
		std::string_view _class		= MF_GetAmxString(amx, params[1], 0, nullptr);
		std::string_view _base		= MF_GetAmxString(amx, params[2], 1, nullptr);
		int32_t _version_no = params[3];

		if (!utils::IsLegit(_class))
		{
			MF_LogError(amx, AMX_ERR_NATIVE, "%s: Not a legit class name", _class.data());
			return 0;	// no success
		}

		if (std::weak_ptr<Class> dup_class = Manager::Instance()->ToClass(_class); !dup_class.expired())
		{
			MF_LogError(amx, AMX_ERR_NATIVE, "%s: Duplicate class", _class.data());
			return 0;	// no success
		}

		std::weak_ptr<Class> super = Manager::Instance()->ToClass(_base);
		if (!_base.empty() && super.expired())
		{
			MF_LogError(amx, AMX_ERR_NATIVE, "%s: Base class (%s) not found", _class.data(), _base.data());
			return 0;	// no success
		}

		Class new_class(_version_no, super);
		Manager::Instance()->NewClass(_class, std::move(new_class));

		return 1;	// success
	}


	// native oo_decl_ctor(const class[], const name[], any: ...);
	// e.g.
	// oo_decl_ctor("Derived",	"Ctor_0arg");
	// oo_decl_ctor("Base",		"Ctor_4arg", OO_CELL, OO_VECTOR, OO_ARRAY[5], OO_STRING);
	cell AMX_NATIVE_CALL native_decl_ctor(AMX *amx, cell params[])
	{
		std::string_view _class	= MF_GetAmxString(amx, params[1], 0, nullptr);
		std::string_view _name	= MF_GetAmxString(amx, params[2], 1, nullptr);

		std::shared_ptr<Class> pclass = Manager::Instance()->ToClass(_class).lock();
		if (pclass == nullptr)
		{
			MF_LogError(amx, AMX_ERR_NATIVE, "%s@%s(...): Class not found", _class.data(), _name.data());
			return 0;	// no success
		}

		if (!utils::IsLegit(_name))
		{
			MF_LogError(amx, AMX_ERR_NATIVE, "%s@%s(...): Not a legit ctor name", _class.data(), _name.data());
			return 0;	// no success
		}

		std::string public_name(_class.length() + 1 + _name.length(), ' ');
		public_name.replace(0, _class.length(), _class);
		public_name[_class.length()] = '@';
		public_name.replace(_class.length() + 1, _name.length(), _name);

		Ctor ctor;
		{
			if (MF_AmxFindPublic(amx, public_name.c_str(), &ctor.public_index) == AMX_ERR_NOTFOUND)
			{
				MF_LogError(amx, AMX_ERR_NATIVE, "%s(...): Public not found", public_name.c_str());
				return 0;
			}

			uint8_t num_args = params[0] / sizeof(cell) - 2;
			//ctor.arg_sizes.resize(num_args);
			for (uint8_t i = 1u; i <= num_args; i++)
			{
				int8_t size = (*MF_GetAmxAddr(amx, params[i + 2]));
				if (!utils::IsLegitSize(size))
				{
					MF_LogError(amx, AMX_ERR_NATIVE, "%s@%s(...): Not a legit size (%d)", _class.data(), _name.data(), size);
					return 0;	// no success
				}


				ctor.arg_sizes.push_back(size);
			}

			ctor.public_index = AddMethod(amx, public_name.c_str(), &ctor.arg_sizes);
		}

		//utils::CheckAndLoadAmxScript(ctor.plugin_index);

		pclass->AddCtor(std::move(ctor));
		return 1;	// success
	}


	// native oo_decl_dtor(const class[], const name[], any: ...);
	// e.g.
	// oo_decl_dtor("Base",	"Dtor");
	cell AMX_NATIVE_CALL native_decl_dtor(AMX *amx, cell params[])
	{
		std::string_view _class	= MF_GetAmxString(amx, params[1], 0, nullptr);
		std::string_view _name	= MF_GetAmxString(amx, params[2], 1, nullptr);

		std::shared_ptr<Class> pclass = Manager::Instance()->ToClass(_class).lock();
		if (pclass == nullptr)
		{
			MF_LogError(amx, AMX_ERR_NATIVE, "%s@%s(...): Class not found", _class.data(), _name.data());
			return 0;	// no success
		}

		if (!utils::IsLegit(_name))
		{
			MF_LogError(amx, AMX_ERR_NATIVE, "%s@%s(...): Not a legit dtor name", _class.data(), _name.data());
			return 0;	// no success
		}

		std::string public_name(_class.length() + 1 + _name.length(), ' ');
		public_name.replace(0, _class.length(), _class);
		public_name[_class.length()] = '@';
		public_name.replace(_class.length() + 1, _name.length(), _name);

		Dtor dtor;
		{
			if (MF_AmxFindPublic(amx, public_name.c_str(), &dtor.public_index) == AMX_ERR_NOTFOUND)
			{
				MF_LogError(amx, AMX_ERR_NATIVE, "%s(): Public not found", public_name.c_str());
				return 0;
			}

			dtor.public_index = AddMethod(amx, public_name.c_str());
		}

		//utils::CheckAndLoadAmxScript(dtor.plugin_index);

		pclass->AssignDtor(std::move(dtor));
		return 1;	// success
	}


	// native oo_decl_msg(const class[], const name[], any: ...);
	// e.g.
	// oo_decl_msg("Player", "EmitSound", OO_CELL, OO_VECTOR, OO_FLOAT);
	cell AMX_NATIVE_CALL native_decl_msg(AMX *amx, cell params[])
	{
		std::string_view _class = MF_GetAmxString(amx, params[1], 0, nullptr);
		std::string_view _name	= MF_GetAmxString(amx, params[2], 1, nullptr);

		std::shared_ptr<Class> pclass = Manager::Instance()->ToClass(_class).lock();
		if (pclass == nullptr)
		{
			MF_LogError(amx, AMX_ERR_NATIVE, "%s@%s(...): Class not found", _class.data(), _name.data());
			return 0;	// no success
		}

		if (!utils::IsLegit(_name))
		{
			MF_LogError(amx, AMX_ERR_NATIVE, "%s@%s(...): Not a legit method name", _class.data(), _name.data());
			return 0;	// no success
		}

		std::string public_name(_class.length() + 1 + _name.length(), ' ');
		public_name.replace(0, _class.length(), _class);
		public_name[_class.length()] = '@';
		public_name.replace(_class.length() + 1, _name.length(), _name);

		//::MessageBoxA(nullptr, public_name.c_str(), "", MB_OK);

		Method mthd;
		{
			if (MF_AmxFindPublic(amx, public_name.c_str(), &mthd.public_index) == AMX_ERR_NOTFOUND)
			{
				MF_LogError(amx, AMX_ERR_NATIVE, "%s(...): Public not found", public_name.c_str());
				return 0;
			}

			//::MessageBoxA(nullptr, public_name.c_str(), "2", MB_OK);

			uint8_t num_args = params[0] / sizeof(cell) - 2;
			//mthd.arg_sizes.resize(num_args);
			for (uint8_t i = 1u; i <= num_args; i++)
			{
				int8_t size = (*MF_GetAmxAddr(amx, params[i + 2]));
				if (!utils::IsLegitSize(size))
				{
					MF_LogError(amx, AMX_ERR_NATIVE, "%s@%s(...): Not a legit size (%d)", _class.data(), _name.data(), size);
					return 0;	// no success
				}

				mthd.arg_sizes.push_back(size);
			}

			mthd.public_index = AddMethod(amx, public_name.c_str(), &mthd.arg_sizes);
		}

		pclass->AddMethod(_name, std::move(mthd));
		return 1;	// success
	}


	// native oo_decl_ivar(const class[], const name[], size);
	// e.g.
	// oo_decl_ivar("Player", "m_Items", OO_ARRAY[4]);
	cell AMX_NATIVE_CALL native_decl_ivar(AMX *amx, cell params[])
	{
		std::string_view _class	= MF_GetAmxString(amx, params[1], 0, nullptr);
		std::string_view _name	= MF_GetAmxString(amx, params[2], 1, nullptr);
		int8_t size				= params[3];

		std::shared_ptr<Class> pclass = Manager::Instance()->ToClass(_class).lock();
		if (pclass == nullptr)
		{
			MF_LogError(amx, AMX_ERR_NATIVE, "%s@%s(...): Class not found", _class.data(), _name.data());
			return 0;	// no success
		}

		if (!utils::IsLegit(_name))
		{
			MF_LogError(amx, AMX_ERR_NATIVE, "%s@%s(...): Not a legit method name", _class.data(), _name.data());
			return 0;	// no success
		}

		if (!utils::IsLegitSize(size))
		{
			MF_LogError(amx, AMX_ERR_NATIVE, "%s@%s(...): Not a legit size (%d)", _class.data(), _name.data(), size);
			return 0;	// no success
		}

		pclass->AddIvar(_name, size);
		return 1;	// success
	}


	// native oo_isa(Obj:this, const super[]);
	// e.g.
	// oo_isa(derived, "Base");
	cell AMX_NATIVE_CALL native_isa(AMX *amx, cell params[])
	{
		ObjectHash _this		= params[1];
		std::string_view _super	= MF_GetAmxString(amx, params[2], 0, nullptr);

		std::shared_ptr<Object> pobj = Manager::Instance()->ToObject(_this).lock();
		if (pobj == nullptr)
		{
			MF_LogError(amx, AMX_ERR_NATIVE, "Object (%d) not found", _this);
			return -1;	// error
		}

		std::weak_ptr<Class> super = Manager::Instance()->ToClass(_super);
		if (super.expired())
		{
			MF_LogError(amx, AMX_ERR_NATIVE, "Super class (%s) not found", _super.data());
			return -1;	// error
		}

		return pobj->isa.lock()->IsSubclassOf(super) ? 1 : 0;
	}


	// native oo_subclass_of(const sub[], const super[]);
	// e.g.
	// oo_subclass_of("Derived", "Base");
	cell AMX_NATIVE_CALL native_subclass_of(AMX *amx, cell params[])
	{
		std::string_view _sub	= MF_GetAmxString(amx, params[1], 0, nullptr);
		std::string_view _super	= MF_GetAmxString(amx, params[2], 1, nullptr);

		std::shared_ptr<Class> psub = Manager::Instance()->ToClass(_sub).lock();
		if (psub == nullptr)
		{
			MF_LogError(amx, AMX_ERR_NATIVE, "Sub Class (%s) not found", _sub.data());
			return -1;	// error
		}

		std::weak_ptr<Class> super = Manager::Instance()->ToClass(_super);
		if (super.expired())
		{
			MF_LogError(amx, AMX_ERR_NATIVE, "Super Class (%s) not found", _super.data());
			return -1;	// error
		}

		return psub->IsSubclassOf(super);
	}


	// native Obj:oo_new(const class[], any: ...);
	// e.g.
	// oo_new("Player", "player name", 100, 100.0);
	cell AMX_NATIVE_CALL native_new(AMX *amx, cell params[])
	{
		std::string_view _class = MF_GetAmxString(amx, params[1], 0, nullptr);

		std::shared_ptr<Class> pclass = Manager::Instance()->ToClass(_class).lock();
		if (pclass == nullptr)
		{
			MF_LogError(amx, AMX_ERR_NATIVE, "Class (%s) not found", _class.data());
			return OBJ_NULL;	// 0 - null
		}

		ObjectHash hash = Manager::Instance()->NewObject(pclass);

		auto &&classes = Manager::Instance()->GetClasses();
		auto &&class_name = std::find_if(classes.begin(), classes.end(), [&](auto &&pair) { return pair.second.get() == pclass.get(); })->first;

		uint8_t num_args = params[0] / sizeof(cell) - 1;

		auto result = Manager::Instance()->FindCtor(pclass, num_args);
		if (result == nullptr)
		{
			if (num_args != 0)
			{
				MF_LogError(amx, AMX_ERR_NATIVE, "New %s: No such ctor (#args: %d)", class_name.c_str(), num_args);
				return OBJ_NULL;	// 0- null
			}

			// nullary constructor
		}
		else
		{
			ExecuteMethod(amx, params, params[0] / sizeof(cell) + 1, result->public_index, hash, &result->arg_sizes, 2);
		}

		return hash;
	}


	// native oo_delete(Obj:this);
	// e.g.
	// oo_delete(oo_new("Player"));
	cell AMX_NATIVE_CALL native_delete(AMX *amx, cell params[])
	{
		ObjectHash _this = params[1];

		std::shared_ptr<Object> pobj = Manager::Instance()->ToObject(_this).lock();
		if (pobj == nullptr)
		{
			MF_LogError(amx, AMX_ERR_NATIVE, "Object (%d) not found", _this);
			return 0;
		}

		std::shared_ptr<Class> pcurrent = pobj->isa.lock();
		assert(pcurrent != nullptr);

		// calling any potential dtors (from sub to super classes)
		do
		{
			auto && dtor = pcurrent->dtor;
			if (dtor.public_index != NO_PUBLIC)
			{
				// call destructors
				ExecuteMethod(amx, params, params[0] / sizeof(cell) + 1, dtor.public_index, _this);
			}
		} while ((pcurrent = pcurrent->super_class.lock()) != nullptr);

		Manager::Instance()->DeleteObject(_this);
		return 0;
	}


	// native oo_send(Obj:this, const name[], any: ...);
	// e.g.
	// oo_send(player, "BaseEntity@EmitSound", 100, origin, 1.0);
	// oo_send(player, "EmitSound", 100, origin, 1.0);
	cell AMX_NATIVE_CALL native_send(AMX *amx, cell params[])
	{
		ObjectHash _this		= params[1];
		std::string_view _name	= MF_GetAmxString(amx, params[2], 0, nullptr);

		std::shared_ptr<Object> pobj = Manager::Instance()->ToObject(_this).lock();
		if (pobj == nullptr)
		{
			MF_LogError(amx, AMX_ERR_NATIVE, "Object (%d) not found", _this);
			return 0;
		}

		std::shared_ptr<Class> pisa = pobj->isa.lock();
		assert(pisa != nullptr);

		// calling any super class' method
		auto &&class_limiter_pos = _name.find('@');
		if (class_limiter_pos != std::string_view::npos)
		{
			std::string_view super_name = _name.substr(0, class_limiter_pos);
			std::shared_ptr<Class> psuper = Manager::Instance()->ToClass(super_name).lock();
			if (psuper == nullptr)
			{
				MF_LogError(amx, AMX_ERR_NATIVE, "Call of %s (super): No such class (%s)", _name.data(), super_name.data());
				return 0;
			}
			if (!pisa->IsSubclassOf(psuper))
			{
				auto &&classes = Manager::Instance()->GetClasses();
				auto &&isa_name = std::find_if(classes.begin(), classes.end(), [&](auto &&pair) { return pair.second.get() == pisa.get(); })->first;
				MF_LogError(amx, AMX_ERR_NATIVE, "Call of %s (super): %s is not a super class of %s", _name.data(), super_name.data(), isa_name.data());
				return 0;
			}

			_name = _name.substr(class_limiter_pos + 1);
		}

		auto &&classes = Manager::Instance()->GetClasses();

		auto result = Manager::Instance()->FindMethod(pisa, _name);
		if (result == nullptr)
		{
			auto &&class_name = std::find_if(classes.begin(), classes.end(), [&](auto &&pair) { return pair.second.get() == pisa.get(); })->first;
			MF_LogError(amx, AMX_ERR_NATIVE, "Call of %s@%s: No such method!", class_name.c_str(), _name.data());
			return 0;
		}

		uint8_t num_args = params[0] / sizeof(cell) - 2;
		if (num_args != result->arg_sizes.size())
		{
			auto &&class_name = std::find_if(classes.begin(), classes.end(), [&](auto &&pair) { return pair.second.get() == pisa.get(); })->first;
			MF_LogError(amx, AMX_ERR_NATIVE, "Call of %s@%s: #args doesn't match (expected: %d, now: %d)", class_name.c_str(), _name.data(), result->arg_sizes.size(), num_args);
			return 0;
		}

		cell ret = ExecuteMethod(amx, params, params[0] / sizeof(cell) + 1, result->public_index, _this, &result->arg_sizes, 3);
		return ret;
	}


	// native oo_read(Obj:this, const name[], any: ...);
	// e.g.
	// health = oo_read(player, "m_nHealth");
	// oo_read(player, "BaseEntity@m_nHealth", health);
	// oo_read(player, "m_nHealth", health);
	// oo_read(player, "m_Items", 0, 3, items, 0, 3);
	cell AMX_NATIVE_CALL native_read(AMX *amx, cell params[])
	{
		ObjectHash _this		= params[1];
		std::string_view _name	= MF_GetAmxString(amx, params[2], 0, nullptr);

		std::shared_ptr<Object> pobj = Manager::Instance()->ToObject(_this).lock();
		if (pobj == nullptr)
		{
			MF_LogError(amx, AMX_ERR_NATIVE, "Reading IVar %s: Object (%d) not found", _name.data(), _this);
			return 0;
		}

		std::shared_ptr<Class> pisa = pobj->isa.lock();
		assert(pisa != nullptr);

		auto && ivar_iter = pobj->ivars.find(std::string{ _name });

		if (ivar_iter == pobj->ivars.end())
		{
			auto &&classes = Manager::Instance()->GetClasses();
			auto &&class_name = std::find_if(classes.begin(), classes.end(), [&](auto &&pair) { return pair.second.get() == pisa.get(); })->first;

			MF_LogError(amx, AMX_ERR_NATIVE, "Reading IVar %s@%s: Not found", class_name.c_str(), _name.data());
			return 0;
		}

		auto && ivar = ivar_iter->second;

		uint8_t num_args = params[0] / sizeof(cell) - 2;

		auto && ivar_size = ivar.size();

		if (num_args == 0)	// get by return value
		{
			if (ivar_size == 1)
				return ivar[0];
			else
			{
				auto &&classes = Manager::Instance()->GetClasses();
				auto &&class_name = std::find_if(classes.begin(), classes.end(), [&](auto &&pair) { return pair.second.get() == pisa.get(); })->first;

				MF_LogError(amx, AMX_ERR_NATIVE, "Reading IVar %s@%s: Is not a cell (size: %d), please copy by value instead", class_name.c_str(), _name.data(), ivar_size);
				return 0;
			}
		}
		else				// get by value copying
		{
			if (ivar_size == 1)
			{
				MF_CopyAmxMemory(MF_GetAmxAddr(amx, params[3]), &ivar[0], 1);
				return ivar[0];
			}
			else
			{
				if (num_args < 5)
				{
					auto &&classes = Manager::Instance()->GetClasses();
					auto &&class_name = std::find_if(classes.begin(), classes.end(), [&](auto &&pair) { return pair.second.get() == pisa.get(); })->first;

					MF_LogError(amx, AMX_ERR_NATIVE, "Reading IVar %s@%s: Required at least 5 args to get array values (now: %d)",
						class_name.c_str(), _name.data(), num_args);
					return 0;
				}

				std::size_t from_begin	= *MF_GetAmxAddr(amx, params[3]);
				std::size_t from_end	= *MF_GetAmxAddr(amx, params[4]);
				std::size_t from_diff	= from_end - from_begin;

				std::size_t to_begin	= *MF_GetAmxAddr(amx, params[6]);
				std::size_t to_end		= *MF_GetAmxAddr(amx, params[7]);
				std::size_t to_diff		= to_end - to_begin;

				std::size_t cell_count	= max(0, min(from_diff, to_diff));

				if ((from_begin + cell_count) > ivar_size)
				{
					auto &&classes = Manager::Instance()->GetClasses();
					auto &&class_name = std::find_if(classes.begin(), classes.end(), [&](auto &&pair) { return pair.second.get() == pisa.get(); })->first;

					MF_LogError(amx, AMX_ERR_NATIVE, "Reading IVar %s@%s: Is of size %d, but requested element #%d-%d",
						class_name.c_str(), _name.data(), ivar.size(), from_begin, (from_begin + cell_count));
					return 0;
				}

				MF_CopyAmxMemory(MF_GetAmxAddr(amx, params[5]) + to_begin, &ivar[from_begin], cell_count);
				return ivar[0];
			}
		}


		// support for OO_STRING?
	}


	// native oo_write(Obj:this, const name[], any: ...);
	// e.g.
	// oo_write(player, "BaseEntity@m_nHealth", 100);
	// oo_write(player, "m_nHealth", 100);
	// oo_write(player, "m_Items", 0, 3, items, 0, 3);
	cell AMX_NATIVE_CALL native_write(AMX *amx, cell params[])
	{
		ObjectHash _this		= params[1];
		std::string_view _name	= MF_GetAmxString(amx, params[2], 0, nullptr);

		std::shared_ptr<Object> pobj = Manager::Instance()->ToObject(_this).lock();
		if (pobj == nullptr)
		{
			MF_LogError(amx, AMX_ERR_NATIVE, "Writing IVar %s: Object (%d) not found", _name.data(), _this);
			return 0;
		}

		std::shared_ptr<Class> pisa = pobj->isa.lock();
		assert(pisa != nullptr);

		auto && ivar_iter = pobj->ivars.find(std::string{ _name });

		if (ivar_iter == pobj->ivars.end())
		{
			auto &&classes = Manager::Instance()->GetClasses();
			auto &&class_name = std::find_if(classes.begin(), classes.end(), [&](auto &&pair) { return pair.second.get() == pisa.get(); })->first;

			MF_LogError(amx, AMX_ERR_NATIVE, "Writing IVar %s@%s: Not found", class_name.c_str(), _name.data());
			return 0;
		}

		auto && ivar = ivar_iter->second;

		uint8_t num_args = params[0] / sizeof(cell) - 2;

		auto && ivar_size = ivar.size();

		if (ivar_size == 1)
		{
			if (num_args < 1)
			{
				auto &&classes = Manager::Instance()->GetClasses();
				auto &&class_name = std::find_if(classes.begin(), classes.end(), [&](auto &&pair) { return pair.second.get() == pisa.get(); })->first;

				MF_LogError(amx, AMX_ERR_NATIVE, "Writing IVar %s@%s: Required at least 1 arg to set a cell value (now: %d)",
					class_name.c_str(), _name.data(), num_args);
				return 0;
			}

			ivar[0] = *MF_GetAmxAddr(amx, params[3]);
			return ivar[0];
		}
		else
		{
			if (num_args < 5)
			{
				auto &&classes = Manager::Instance()->GetClasses();
				auto &&class_name = std::find_if(classes.begin(), classes.end(), [&](auto &&pair) { return pair.second.get() == pisa.get(); })->first;

				MF_LogError(amx, AMX_ERR_NATIVE, "Writing IVar %s@%s: Required at least 5 arg to set a cell value (now: %d)",
					class_name.c_str(), _name.data(), num_args);
				return 0;
			}

			std::size_t to_begin	= *MF_GetAmxAddr(amx, params[3]);
			std::size_t to_end		= *MF_GetAmxAddr(amx, params[4]);
			std::size_t to_diff		= (to_end == 0) ? ivar_size : to_end - to_begin;

			std::size_t from_begin	= *MF_GetAmxAddr(amx, params[6]);
			std::size_t from_end	= *MF_GetAmxAddr(amx, params[7]);
			std::size_t from_diff	= (from_end == 0) ? ivar_size : from_end - from_begin;

			std::size_t cell_count	= max(0, min(from_diff, to_diff));

			if ((to_begin + cell_count) > ivar_size)
			{
				auto &&classes = Manager::Instance()->GetClasses();
				auto &&class_name = std::find_if(classes.begin(), classes.end(), [&](auto &&pair) { return pair.second.get() == pisa.get(); })->first;

				MF_LogError(amx, AMX_ERR_NATIVE, "Writing IVar %s@%s: Is of size %d, but requested element #%d-%d",
					class_name.c_str(), _name.data(), ivar.size(), to_begin, (to_begin + cell_count));
				return 0;
			}

			for (std::size_t i = 0; i < cell_count; i++)
				ivar[to_begin + i] = *(MF_GetAmxAddr(amx, params[5]) + from_begin + i);

			return ivar[0];
		}

		// support for OO_STRING?
	}


	// native oo_this();
	// e.g.
	// oo_send(oo_this(), "EmitSound");
	cell AMX_NATIVE_CALL native_this(AMX *amx, cell params[])
	{
		return Manager::Instance()->GetThis();
	}


	// native oo_this_ctor(any: ...);
	// e.g.
	// oo_this_ctor(100, origin, 1.0);
	cell AMX_NATIVE_CALL native_this_ctor(AMX *amx, cell params[])
	{
		return cell();
	}

	cell AMX_NATIVE_CALL native_class_exists(AMX* amx, cell params[])
	{
		std::string_view _class = MF_GetAmxString(amx, params[1], 0, nullptr);

		std::shared_ptr<Class> pclass = Manager::Instance()->ToClass(_class).lock();
		if (pclass == nullptr)
		{
			return 0;	// no success
		}
		
		return 1;
	}

	cell AMX_NATIVE_CALL native_get_class_name(AMX* amx, cell params[])
	{
		ObjectHash _this = params[1];

		std::weak_ptr<Object> pobj = Manager::Instance()->ToObject(_this);
		if (pobj.lock() == nullptr)
		{
			MF_LogError(amx, AMX_ERR_NATIVE, "Object (%d) not found", _this);
			return 0;	// error
		}

		std::string_view classname = Manager::Instance()->GetObjectClassName(pobj);
		if (!classname.empty())
		{
			MF_SetAmxString(amx, params[2], classname.data(), params[3]);
		}

		return 1;
	}

	// native oo_super_ctor(const super[], any: ...);
	// e.g.
	// oo_super_ctor("BaseEntity", 100, origin, 1.0);
	cell AMX_NATIVE_CALL native_super_ctor(AMX *amx, cell params[])
	{
		ObjectHash this_hash = Manager::Instance()->GetThis();

		std::shared_ptr<Object> pthis = Manager::Instance()->ToObject(this_hash).lock();
		if (pthis == nullptr)
		{
			MF_LogError(amx, AMX_ERR_NATIVE, "*this* object is invalid");
			return 0;
		}

		std::string_view _superclass = MF_GetAmxString(amx, params[1], 0, nullptr);

		std::shared_ptr<Class> psuper = Manager::Instance()->ToClass(_superclass).lock();
		if (psuper == nullptr)
		{
			MF_LogError(amx, AMX_ERR_NATIVE, "Class (%s) not found", _superclass.data());
			return 0;
		}

		std::shared_ptr<Class> pisa = pthis->isa.lock();
		assert(pisa != nullptr);

		if (!pisa->IsSubclassOf(psuper))
		{
			auto&& classes = Manager::Instance()->GetClasses();
			auto&& isa_name = std::find_if(classes.begin(), classes.end(), [&](auto&& pair) { return pair.second.get() == pisa.get(); })->first;
			MF_LogError(amx, AMX_ERR_NATIVE, "Call of %s constructor (super): %s is not a super class of %s", _superclass.data(), _superclass.data(), isa_name.data());
			return 0;
		}

		uint8_t num_args = params[0] / sizeof(cell) - 1;

		auto result = Manager::Instance()->FindCtor(psuper, num_args);
		auto&& public_index = result->public_index;
		auto&& arg_sizes = result->arg_sizes;

		if (public_index == NO_PUBLIC)
		{
			if (num_args != 0)
			{
				MF_LogError(amx, AMX_ERR_NATIVE, "%s: No such ctor (#args: %d)", _superclass.data(), num_args);
				return 0;
			}

			// nullary constructor
		}
		else
		{
			ExecuteMethod(amx, params, params[0] / sizeof(cell) + 1, result->public_index, this_hash, &result->arg_sizes, 2);
		}

		return 1;
	}
}