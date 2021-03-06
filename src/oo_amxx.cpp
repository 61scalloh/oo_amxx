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


#include "amxxmodule.h"
#include "oo_class.h"
#include "oo_manager.h"
#include "oo_natives.h"

/** AMXX attach
 * Do native functions init here (MF_AddNatives)
 */
void OnAmxxAttach(void)
{
	static const AMX_NATIVE_INFO oo_natives[] =
	{
		{ "oo_decl_class",	oo::natives::native_decl_class },
		{ "oo_decl_ctor",	oo::natives::native_decl_ctor },
		{ "oo_decl_dtor",	oo::natives::native_decl_dtor },
		{ "oo_decl_msg",	oo::natives::native_decl_msg },
		{ "oo_decl_ivar",	oo::natives::native_decl_ivar },

		{ "oo_isa",			oo::natives::native_isa },
		{ "oo_subclass_of",	oo::natives::native_subclass_of	},

		{ "oo_new",			oo::natives::native_new },
		{ "oo_delete",		oo::natives::native_delete },
		{ "oo_send",		oo::natives::native_send },
		{ "oo_read",		oo::natives::native_read },
		{ "oo_write",		oo::natives::native_write },

		{ "oo_this",		oo::natives::native_this },

		{ "oo_this_ctor",	oo::natives::native_this_ctor },
		{ "oo_super_ctor",	oo::natives::native_super_ctor },

		{ "oo_class_exists",	oo::natives::native_class_exists },
		{ "oo_get_class_name",	oo::natives::native_get_class_name },

		{ nullptr, nullptr }
	};
	MF_AddNatives(oo_natives);
}


/** All plugins loaded
 * Do forward functions init here (MF_RegisterForward)
 */
void OnPluginsLoaded(void)
{
	//::MessageBoxA(nullptr, "0", "", MB_OK);

	MF_ExecuteForward(
		MF_RegisterForward(
			"oo_init",
			ForwardExecType::ET_IGNORE,
			ForwardParam::FP_DONE)
	);

	//::MessageBoxA(nullptr, "1", "", MB_OK);

	MF_PrintSrvConsole("[%s] Classes and their methods:\n", MODULE_LOGTAG);
	for (auto &&cl : oo::Manager::Instance()->GetClasses())
	{
		//::MessageBoxA(nullptr, "a", cl.first.c_str(), MB_OK);
		MF_PrintSrvConsole("    %s (#%p)\n", cl.first.c_str(), static_cast<void *>(cl.second.get()));
		{
			int extra_tabs = 3;
			std::shared_ptr<oo::Class> pderived = cl.second->super_class.lock();
			while (pderived != nullptr)
			{
				MF_PrintSrvConsole("%s-> (#%p)\n", std::string(extra_tabs++ * 4, ' ').c_str(), static_cast<void*>(pderived.get()));
				pderived = pderived->super_class.lock();
			}
		}

		//::MessageBoxA(nullptr, "b", cl.first.c_str(), MB_OK);

		MF_PrintSrvConsole("         <Ctors>\n");
		for (auto &&ct : cl.second->ctors)
		{
			MF_PrintSrvConsole("              %s@%d(", cl.first.c_str(), ct.first);
			int count = 0;
			for (auto && arg : ct.second.arg_sizes)
			{
				if (count++ > 0) MF_PrintSrvConsole(", ");
				MF_PrintSrvConsole(std::to_string(arg).c_str());
			}
			MF_PrintSrvConsole(")\n");
		}

		MF_PrintSrvConsole("         <Methods>\n");
		for (auto &&m : cl.second->methods)
		{
			MF_PrintSrvConsole("              %s@%s(", cl.first.c_str(), m.first.c_str());
			int count = 0;
			for (auto && arg : m.second.arg_sizes)
			{
				if (count++ > 0) MF_PrintSrvConsole(", ");
				MF_PrintSrvConsole(std::to_string(arg).c_str());
			}
			MF_PrintSrvConsole(")\n");
		}

		//::MessageBoxA(nullptr, "c", cl.first.c_str(), MB_OK);

		MF_PrintSrvConsole("         <IVars>\n");
		for (auto &&iv : cl.second->meta_ivars)
			MF_PrintSrvConsole("              %s@%s[%d]\n", cl.first.c_str(), iv.first.c_str(), iv.second);

		//::MessageBoxA(nullptr, "d", cl.first.c_str(), MB_OK);
	}
}

void OnPluginsUnloaded()
{
	oo::Manager::Instance()->Clear();
	MF_PrintSrvConsole("OO Release memory\n");
}

void OnAmxxDetach()
{
}