
#include "windows.h"
#include "rmx_gen.h"
#include "imgui.h"
#include "backends/imgui_impl_dx9.h"
#include "backends/imgui_impl_win32.h"
#include <math.h>
#include "d3d_wrapper.hpp"
#include "d3d_global.hpp"
#include "d3d_matrix_stack.hpp"
#include "d3d_state.hpp"
#include "d3d_helpers.hpp"
#include <string>

#define GVAR_SHOW_IMGUI "rmx_show_imgui"

enum exc_wprocnmouse_e
{
	WNDPROC_SET_IMGUI,
	WNDPROC_RESTORE_GAME
};

static BOOL g_initialised = FALSE;
static BOOL g_visible = FALSE;
static BOOL g_in_mouse_val = FALSE;
static BOOL g_use_shortcut_keys = TRUE;
static BOOL g_debug_controls = FALSE;
static ImVec2 g_mouse_click_pos;
static BOOL g_mouse_click_new = FALSE;

static void *g_hwnd = NULL;
static IDirect3DDevice9* g_device = NULL;
static WNDPROC g_game_wndproc = NULL;
static int window_center_x = 0;
static int window_center_y = 0;
static key_inputs_t keys = { 0 };

static ImGuiIO *io;
static ImVec4 clear_color = ImVec4(0.45f, 0.55f, 0.60f, 1.00f);

//extern "C" cvar_t  *in_mouse;
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

static LRESULT WINAPI wnd_proc_hk( HWND hWnd, UINT message_type, WPARAM wparam, LPARAM lparam );
static void exchange_wndproc_and_mouse( enum exc_wprocnmouse_e action );

#pragma warning( push )
#pragma warning( disable : 4305)

static void do_draw()
{
	static float f = 0.0f;
	static int counter = 0;
	static bool no_background = 0;
	static bool no_scrollbar = 1;
	ImGuiWindowFlags flags = 0;
	if ( no_background ) flags = flags | ImGuiWindowFlags_NoBackground;
	if ( no_scrollbar ) flags = flags | ImGuiWindowFlags_NoScrollbar;

	ImGui::Begin( "Game config", NULL, flags );                          // Create a window and append into it.

	if ( ImGui::Button( "Play!" ) )
	{
		exchange_wndproc_and_mouse(WNDPROC_RESTORE_GAME);
	}
	ImGui::SameLine();
	if ( ImGui::Button( "Close" ) )
	{
		if ( g_visible )
		{
			g_visible = FALSE;
			exchange_wndproc_and_mouse( WNDPROC_RESTORE_GAME );
		}
	}
	ImGui::SameLine();
	ImGui::Checkbox("No background", &no_background);
	ImGui::SameLine();
	ImGui::Checkbox("No scrollbar", &no_scrollbar);

	const float* pos = qdx_4imgui_flashlight_position_3f();
	const float* dir = qdx_4imgui_flashlight_direction_3f();
	ImGui::Text( "Position  %.3f %.3f %.3f", pos[0], pos[1], pos[2] );
	ImGui::Text( "Direction %.3f %.3f %.3f", dir[0], dir[1], dir[2] );
	if ( ImGui::CollapsingHeader( "Flashlight Config" ) )
	{
		ImGui::SeparatorText("Placement");
		float* pos_off = qdx_4imgui_flashlight_position_off_3f();
		float* dir_off = qdx_4imgui_flashlight_direction_off_3f();
		ImGui::DragFloat3( "Position  Offset", pos_off, 0.01, -100, 100 );
		ImGui::DragFloat2( "Direction Offset", dir_off, 0.001, -1, 1 );
		ImGui::SeparatorText("Spot 1");
		ImGui::ColorEdit3( "Color##1", qdx_4imgui_flashlight_colors_3f(0), ImGuiColorEditFlags_Float );
		ImGui::DragFloat( "Radiance##1", qdx_4imgui_flashlight_radiance_1f(0), 1, 0, 10000 );
		ImGui::DragFloat( "Angle##1", qdx_4imgui_flashlight_coneangles_1f(0), 0.1, 0, 180 );
		ImGui::DragFloat( "Soft##1", qdx_4imgui_flashlight_conesoft_1f(0), 0.001, 0, 1 );
		ImGui::DragFloat( "Volumetric##1", qdx_4imgui_flashlight_volumetric_1f(0), 0.1, 0, 10 );
		ImGui::SeparatorText("Spot 2");
		ImGui::ColorEdit3( "Color##2", qdx_4imgui_flashlight_colors_3f(1), ImGuiColorEditFlags_Float );
		ImGui::DragFloat( "Radiance##2", qdx_4imgui_flashlight_radiance_1f(1), 1, 0, 10000 );
		ImGui::DragFloat( "Angle##2", qdx_4imgui_flashlight_coneangles_1f(1), 0.1, 0, 180 );
		ImGui::DragFloat( "Soft##2", qdx_4imgui_flashlight_conesoft_1f(1), 0.001, 0, 1 );
		ImGui::DragFloat( "Volumetric##2", qdx_4imgui_flashlight_volumetric_1f(1), 0.1, 0, 10 );
		ImGui::SeparatorText("Spot 3");
		ImGui::ColorEdit3( "Color##3", qdx_4imgui_flashlight_colors_3f(2), ImGuiColorEditFlags_Float );
		ImGui::DragFloat( "Radiance##3", qdx_4imgui_flashlight_radiance_1f(2), 1, 0, 10000 );
		ImGui::DragFloat( "Angle##3", qdx_4imgui_flashlight_coneangles_1f(2), 0.1, 0, 180 );
		ImGui::DragFloat( "Soft##3", qdx_4imgui_flashlight_conesoft_1f(2), 0.001, 0, 1 );
		ImGui::DragFloat( "Volumetric##3", qdx_4imgui_flashlight_volumetric_1f(2), 0.1, 0, 10 );

		ImGui::NewLine();
		if ( ImGui::Button( "Save Configuration" ) )
		{
			qdx_flashlight_save();
		}
	}

	if ( ImGui::CollapsingHeader( "Lights Global Config" ) )
	{

		ImGui::SeparatorText( "DynamicLight Radiance:" );
		ImGui::Text( "Radiance = color * (BASE + intensity * SCALE)" );
		ImGui::DragFloat( "BASE##1", qdx_4imgui_radiance_dynamic_1f(), 10, 0, 50000 );
		ImGui::DragFloat( "SCALE##1", qdx_4imgui_radiance_dynamic_scale_1f(), 0.01, 0, 10 );
		ImGui::Text( "Radius = RADIUS + intensity * RADIUS_SCALE" );
		ImGui::DragFloat( "RADIUS##1", qdx_4imgui_radius_dynamic_1f(), 0.1, 0, 10 );
		ImGui::DragFloat( "RADIUS_SCALE##1", qdx_4imgui_radius_dynamic_scale_1f(), 0.0005, 0, 1 );

		ImGui::SeparatorText( "Corona Radiance:" );
		if(ImGui::DragFloat( "BASE##2", qdx_4imgui_radiance_coronas_1f(), 10, 0, 50000 ))
		{
			qdx_lights_clear( LIGHT_CORONA );
		}
		if(ImGui::DragFloat( "RADIUS##2", qdx_4imgui_radius_coronas_1f(), 0.1, 0, 10 ))
		{
			qdx_lights_clear( LIGHT_CORONA );
		}

		ImGui::NewLine();
		if ( ImGui::Button( "Save to Global" ) )
		{
			qdx_radiance_save( true );
		}
		ImGui::SameLine();
		if ( ImGui::Button( " Save to Map " ) )
		{
			qdx_radiance_save( false );
		}
	}

	if ( ImGui::CollapsingHeader( "Light Override" ) )
	{
		static int light_type = 0;
		const light_type_e map_light_type[] = { LIGHT_DYNAMIC, LIGHT_CORONA, LIGHT_NEW };
		static int32_t selected_index = 0;
		static bool flash_it = false;
		uint64_comp_t idval;
		static light_override_t* ovr = NULL;
		static float original_color[3] = { 0 };

#define FLASH_IT() if(ovr) { flash_it = 1; original_color[0] = ovr->color[0]; original_color[1] = ovr->color[1]; original_color[2] = ovr->color[2]; }
#define UNFLASH_IT() if(ovr) { flash_it = 0; ovr->color[0] = original_color[0]; ovr->color[1] = original_color[1]; ovr->color[2] = original_color[2]; ovr->updated = 1; }

		if ( 0 == qdx_4imgui_light_picking_count() )
		{
			ovr = NULL;
		}

		ImGui::Text( "Hint: Select type, then Scan for closest lights" );
		if ( ImGui::Combo( "##11", &light_type, "Scan DynamicLights\0Scan Coronas\0Scan NewLights\0\0" ) )
		{
			//require a Scan after this changes
			if ( flash_it )
				UNFLASH_IT();
			qdx_4imgui_light_picking_clear();
			ovr = NULL;
		}
		if ( ImGui::Button( " Scan " ) )
		{
			selected_index = 0;
			if ( flash_it )
				UNFLASH_IT();
			qdx_light_scan_closest_lights(map_light_type[light_type]);
			ovr = qdx_4imgui_light_get_override( qdx_4imgui_light_picking_id( selected_index ), map_light_type[light_type] );
		}
		ImGui::SameLine();
		if ( ImGui::Checkbox( "Flash-it", &flash_it ) )
		{   //value changed
			if ( ovr == NULL )
			{   //don't allow it
				flash_it = 0;
			}
			else
			{
				if ( flash_it ) { FLASH_IT(); }
				else { UNFLASH_IT(); }
			}
		}
		/* Do the flashing here */
		if ( ovr && flash_it )
		{
			float xval = sinf( rmx_milliseconds()/100.0f );
			xval += 1.0f;
			xval *= 0.5f;
			ovr->color[0] = xval;
			ovr->color[1] = xval;
			ovr->color[2] = xval;
			ovr->updated = 1;
		}
		idval.ll = qdx_4imgui_light_picking_id( selected_index );
		ImGui::Text( "Active ID: 0x%x%x", idval.u32[1], idval.u32[0] );

		/* Controls are active if light was found and we are not flashing-it */
		if ( qdx_4imgui_light_picking_count() && flash_it == 0 )
		{
			if ( ImGui::SliderInt( "Closest", &selected_index, 0, qdx_4imgui_light_picking_count() - 1 ) )
			{
				ovr = qdx_4imgui_light_get_override( qdx_4imgui_light_picking_id( selected_index ), map_light_type[light_type] );
			}
			if ( ovr )
			{
				if ( ImGui::DragFloat3( "Position  Offset##10", ovr->position_offset, 0.1, -200, 200 ) ) ovr->updated = 1;
				if ( ImGui::ColorEdit3( "Color##10", ovr->color, ImGuiColorEditFlags_Float ) ) ovr->updated = 1;
				if ( ImGui::DragFloat( "RADIANCE##10", &ovr->radiance_base, 10, 0, 50000 ) ) ovr->updated = 1;
				if ( light_type == 0 )
				{
					if ( ImGui::DragFloat( "RADIANCE_SCALE##10", &ovr->radiance_scale, 0.01, 0, 10 ) ) ovr->updated = 1;
				}
				else
				{
					ImGui::NewLine();
				}
				if ( ImGui::DragFloat( "RADIUS##10", &ovr->radius_base, 0.1, 0, 10 ) ) ovr->updated = 1;
				if ( light_type == 0 )
				{
					if ( ImGui::DragFloat( "RADIUS_SCALE##10", &ovr->radius_scale, 0.0005, 0, 1 ) ) ovr->updated = 1;
				}
				else
				{
					ImGui::NewLine();
				}

				if ( ImGui::DragFloat( "VOLUMETRIC_SCALE", &ovr->volumetric_scale, 0.1, 0, 10 ) ) ovr->updated = 1;
				if ( ImGui::Button( " Save " ) )
				{
					qdx_light_override_save( ovr );
				}
				ImGui::SameLine();
				if ( ImGui::Button( " Reset ##OVR" ) )
				{
					qdx_4imgui_light_clear_override( idval.ll );
					ovr = qdx_4imgui_light_get_override( qdx_4imgui_light_picking_id( selected_index ), map_light_type[light_type] );
				}
				ImGui::SameLine();
				if ( ImGui::Button( "Save ALL" ) )
				{
					qdx_light_override_save_all();
				}
			}
		}
	}

	if (ImGui::CollapsingHeader("Fallback Light"))
	{
		static float rgb[3] = { 1, 1, 1 };
		static float cnv[3] = { 1, 1, 1 };
		static float scale = 1;
		static bool enabled = true;
		bool updated = false;
		if (ImGui::Checkbox("Enabled", &enabled)) updated = true;
		if (ImGui::ColorEdit3("Color##20", rgb, ImGuiColorEditFlags_Float)) updated = true;
		if (ImGui::DragFloat("Intensity", &scale, 0.02, 0, 5)) updated = true;
		if (updated)
		{
			float multv = max(max(rgb[0], rgb[1]), rgb[2]);
			if(multv != 0)
				multv = scale / multv;
			cnv[0] = rgb[0] * multv;
			cnv[1] = rgb[1] * multv;
			cnv[2] = rgb[2] * multv;
			rmx_distant_light_radiance(cnv[0], cnv[1], cnv[2], enabled);
		}
		ImGui::Text("Final %.3f %.3f %.3f", cnv[0], cnv[1], cnv[2]);
	}

#ifdef SURFACE_SEPARATION
	if ( ImGui::CollapsingHeader( "Surface Separation" ) )
	{
		int selected_shader = r_draw1shader->integer;
		int total_shaders = tr.numShaders;
		int shader_info = -99;
		const char *shader_name = qdx_4imgui_shader_info( &shader_info );
		int total_aabbs = 0;
		int *selected_aabb = qdx_4imgui_surface_aabb_selection( &total_aabbs );
		ImGui::Text( "Hint: Press <Draw AABBs> then switch shaders +/-" );
		ImGui::Text( " after <Separate>, reload map to see results" );
		static int prev_shader = -1;
		static int shader_dir = 0;
		static bool all_aabb_sizes = 0;
		if ( r_showaabbs->integer > 1 )
		{
			all_aabb_sizes = 1;
		}

		if ( shader_dir != 0 )
		{
			if ( shader_name[0] != 0 )
			{
				shader_dir = 0;
			}
			else if ( selected_shader <= -1 || selected_shader >= total_shaders )
			{
				shader_dir = 0;
				selected_shader = -1;
			}
			else
			{
				selected_shader = selected_shader + shader_dir;
			}
		}

		ImGui::Text( "Draw AABBs:" );
		ImGui::SameLine();

		if ( ImGui::Button( "nearby" ) )
		{
			std::string cmd = "r_nobackfacecull 1; r_clear 2";
			if ( r_showaabbs->integer == 0 )
			{
				cmd.append( "; r_showaabbs " );
				cmd.append( all_aabb_sizes ? "2" : "1" );
			}
			if ( r_novis->integer != 0 )
				cmd.append("; r_novis 0");
			if ( r_nocull->integer != 0 )
				cmd.append("; r_nocull 0");
			if ( r_ignoreFastPath->integer == 0 )
			{
				cmd.append( "; r_ignoreFastPath 1; echo You need vid_restart to apply changes!" );
				//g_visible = FALSE;
				//exchange_wndproc_and_mouse( WNDPROC_RESTORE_GAME );
			}
			ri.Cmd_ExecuteText( EXEC_APPEND, cmd.c_str() );
		}
		ImGui::SetItemTooltip("novis/nocull 0");
		ImGui::SameLine();
		if ( ImGui::Button( "complete" ) )
		{
			std::string cmd = "r_nobackfacecull 1; r_clear 2";
			if ( r_showaabbs->integer == 0 )
			{
				cmd.append( "; r_showaabbs " );
				cmd.append( all_aabb_sizes ? "2" : "1" );
			}
			if ( r_novis->integer == 0 )
				cmd.append("; r_novis 1");
			if ( r_nocull->integer == 0 )
				cmd.append("; r_nocull 1");
			if ( r_ignoreFastPath->integer == 0 )
			{
				cmd.append( "; r_ignoreFastPath 1; vid_restart" );
				g_visible = FALSE;
				exchange_wndproc_and_mouse( WNDPROC_RESTORE_GAME );
			}
			ri.Cmd_ExecuteText( EXEC_APPEND, cmd.c_str() );
		}
		ImGui::SetItemTooltip("novis/nocull 1");
		ImGui::SameLine();
		if ( ImGui::Checkbox( "all sizes", &all_aabb_sizes ) )
		{
			if ( all_aabb_sizes )
			{
				if ( r_showaabbs->integer == 1 )
				{
					ri.Cvar_Set( "r_showaabbs", "2" );
					qdx_surface_aabb_clearall();
				}
			}
			else
			{
				if ( r_showaabbs->integer > 1 )
				{
					ri.Cvar_Set( "r_showaabbs", "1" );
					qdx_surface_aabb_clearall();
				}
			}
		}
		ImGui::SetItemTooltip("show unmerged AABBs for all small surfaces");
		ImGui::Text( "Shader: %d out of: %d", selected_shader, total_shaders );
		ImGui::Text( "Shader name: %s", shader_name );
		ImGui::Text( "Shader info: %d", shader_info );
		ImGui::Text( "Filter:" );
		ImGui::SameLine();
		if ( ImGui::Button( " None " ) )
		{
			*selected_aabb = 0;
			shader_dir = 0;
			prev_shader = selected_shader;
			selected_shader = -1;
			qdx_surface_store_shader_info( "", -99 );
		}
		ImGui::SameLine();
		if ( ImGui::Button( " Shader- " ) )
		{
			*selected_aabb = 0;
			prev_shader = selected_shader;
			if ( selected_shader == -1 )
			{
				shader_dir = -1;
				selected_shader = total_shaders - 1;
				qdx_surface_store_shader_info( "", -99 );
			}
			else if ( selected_shader > -1 )
			{
				shader_dir = -1;
				selected_shader = selected_shader + shader_dir;
				qdx_surface_store_shader_info( "", -99 );
			}
		}
		ImGui::SameLine();
		if ( ImGui::Button( " Shader+ " ) )
		{
			*selected_aabb = 0;
			prev_shader = selected_shader;
			if ( selected_shader + 1 == total_shaders )
			{
				shader_dir = 0;
				selected_shader = -1;
				qdx_surface_store_shader_info( "", -99 );
			}
			else if ( selected_shader +1 < total_shaders )
			{
				shader_dir = 1;
				selected_shader = selected_shader + shader_dir;
				qdx_surface_store_shader_info( "", -99 );
			}
		}
		ImGui::SameLine();
		ImGui::SetNextItemWidth( 50 );
		int input0 = selected_shader;
		if ( ImGui::InputInt( "##ManualInputSelectedShader", &input0, 0, 0, ImGuiInputTextFlags_AutoSelectAll ) )
		{
			if ( input0 >= -1 && input0 +1 < total_shaders )
			{
				shader_dir = 1;
				//prev_shader = selected_shader;
				selected_shader = input0;
				qdx_surface_store_shader_info( "", -99 );
			}
		}
		ImGui::SameLine();
		{
			char prev_txt[12];
			snprintf( prev_txt, sizeof( prev_txt ), "Prev: %d", prev_shader );
			if ( ImGui::Button( prev_txt ) )
			{
				selected_shader = prev_shader;
			}
		}
		ImGui::SliderInt( "Selection", selected_aabb, 0, total_aabbs );
		static float mergedist = r_aabb_mergedist->value;
		if ( ImGui::SliderFloat( "Merge Dist", &mergedist, 0, 100 ) )
		{
			char value[10];
			snprintf( value, sizeof( value ), "%.3f", mergedist );
			ri.Cvar_Set( "r_aabb_mergedist", value );
			qdx_surface_aabb_clearall();
		}
		static char savehint[128] = { 0 };
		const char *savehint_hint = "Write this text to the ini, for easy reference";
		ImGui::InputTextWithHint( "INI Hint", savehint_hint,	savehint, sizeof( savehint ) );
		ImGui::SetItemTooltip(savehint_hint);
		if ( ImGui::Button( "Separate" ) )
		{
			qdx_4imgui_surface_aabb_saveselection(savehint);
		}
		if ( selected_shader != r_draw1shader->integer )
		{
			char value[6];
			snprintf( value, sizeof( value ), "%d", selected_shader );
			ri.Cvar_Set( "r_draw1shader", value );
		}
	}
#endif
	
	ImGui::NewLine(); ImVec2 toggle_sz( 150, 0 );
	if ( ImGui::Button( "Toggle Flashlight", toggle_sz ) )
	{
		rmx_flashlight_enable();
	}
#ifdef DEBUG_HELPERS
	/*==========================
	* Togglers
	*==========================*/
	ImGui::NewLine(); ImVec2 toggle_sz( 150, 0 );
	if ( ImGui::Button( "Toggle Flashlight", toggle_sz ) )
	{
		ri.Cvar_Set( "r_rmx_flashlight", (r_rmx_flashlight->integer ? "0" : "1") );
	}
	ImGui::SameLine();
	ImGui::Text( "Value: %s", r_rmx_flashlight->integer ? "on" : "off");
	if ( ImGui::Button( "Toggle DynamicLight", toggle_sz ) )
	{
		ri.Cvar_Set( "r_rmx_dynamiclight", (r_rmx_dynamiclight->integer ? "0" : "1") );
	}
	ImGui::SameLine();
	ImGui::Text( "Value: %s", r_rmx_dynamiclight->integer ? "on" : "off" );
	//ImGui::SameLine();
	if ( ImGui::Button( "Toggle Coronas", toggle_sz ) )
	{
		ri.Cvar_Set( "r_rmx_coronas", (r_rmx_coronas->integer ? "0" : "1") );
	}
	ImGui::SameLine();
	ImGui::Text( "Value: %s", r_rmx_coronas->integer ? "on" : "off" );

	/*==========================
	* Quick Actions
	*==========================*/
	ImGui::NewLine();
	if ( ImGui::Button( "QUIT" ) )
	{
		g_visible = FALSE;
		exchange_wndproc_and_mouse(WNDPROC_RESTORE_GAME);
		ri.Cmd_ExecuteText(EXEC_APPEND, "quit");
	}
	ImGui::SameLine();
	if ( ImGui::Button( "NOCLIP" ) )
	{
		ri.Cmd_ExecuteText(EXEC_APPEND, "noclip");
	}
	ImGui::SameLine();
	if ( ImGui::Button( "NOTARGET" ) )
	{
		ri.Cmd_ExecuteText(EXEC_APPEND, "notarget");
	}
	ImGui::SameLine();
	if ( ImGui::Button( "GIVE ALL" ) )
	{
		ri.Cmd_ExecuteText(EXEC_APPEND, "give all");
	}
	if ( g_in_mouse_val == 0 )
	{
		ImGui::SameLine();
		if ( ImGui::Button( "MOUSE FIX" ) )
		{
			g_in_mouse_val = 1;
			ri.Cvar_Set( "in_mouse", "1" );
			IN_StartupMouse();
			ri.Cvar_Set( "in_mouse", "0" );
		}
	}

	ImGui::NewLine();
	if ( ImGui::Button( " GPU Skinning " ) )
	{
		char newval[2] = "0";
		if ( r_gpuskinning->integer < 2 )
			newval[0] = '1' + r_gpuskinning->integer;
		ri.Cvar_Set( "r_gpuskinning", newval );
	}
	ImGui::SameLine();
	ImGui::Text( "Value: %s", r_gpuskinning->string );
	/*==========================
	* Debug Controls
	*==========================*/
	if ( g_debug_controls )
	{
		if ( ImGui::Button( " MeshAnim " ) )
		{
			ri.Cvar_Set( "r_nomeshanim", (r_nomeshanim->integer ? "0" : "1") );
		}
		ImGui::SameLine();
		ImGui::Text( "Value: %s", r_nomeshanim->integer == 0 ? "on" : "off" );
		ImGui::SliderInt( "Hide model type", helper_value( 0 ), -1, 4 );
		ImGui::SliderInt( "Single Bone Vertexes", helper_value( 1 ), -1, 4 );
		ImGui::Text( "Num entities: %d", tr.refdef.num_entities );
	}
#endif

	/*==========================
	* Stats
	*==========================*/
	ImGui::NewLine();
	ImGui::Text( "App average %.3f ms/frame (%.1f FPS)", 1000.0f / io->Framerate, io->Framerate );
	ImGui::End();
}

#pragma warning( pop )

void qdx_imgui_init(void *hwnd, void *device)
{
	if ( !g_initialised )
	{
		g_initialised = TRUE;
		g_hwnd = hwnd;
		g_device = (IDirect3DDevice9*)device;
		g_use_shortcut_keys = 1;// qdx_readsetting( "imgui_allow_shortcut_keys", g_use_shortcut_keys );
		g_debug_controls = 0;// qdx_readsetting( "imgui_debug_controls", g_debug_controls );

		IMGUI_CHECKVERSION();
		ImGui::CreateContext();
		io = &ImGui::GetIO();
		io->ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;     // Enable Keyboard Controls
		io->ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;      // Enable Gamepad Controls

																   // Setup Dear ImGui style
		ImGui::StyleColorsDark();
		//ImGui::StyleColorsLight();

		// Setup Platform/Renderer backends
		ImGui_ImplWin32_Init( hwnd );
		ImGui_ImplDX9_Init( (IDirect3DDevice9*)device );
	}
}

void qdx_imgui_deinit()
{
	if ( g_initialised )
	{
		g_initialised = FALSE;
		g_hwnd = NULL;
		g_device = NULL;

		if ( g_visible )
		{
			g_visible = FALSE;
			exchange_wndproc_and_mouse(WNDPROC_RESTORE_GAME);
		}

		ImGui_ImplDX9_Shutdown();
		ImGui_ImplWin32_Shutdown();
		ImGui::DestroyContext();
	}
}

void qdx_imgui_draw()
{
	if ( g_use_shortcut_keys )
	{
		keys = keypress_get();
		if ( keys.imgui || (keys.alt && keys.c) )
		{   //toggle cvar
			g_visible = !g_visible;
			
			if ( g_visible )
			{
				//void *game_wndproc = (WNDPROC)(GetWindowLongPtr(HWND(g_hwnd), GWLP_WNDPROC));
				exchange_wndproc_and_mouse(WNDPROC_SET_IMGUI);
			}
			else
			{
				exchange_wndproc_and_mouse(WNDPROC_RESTORE_GAME);
			}
		}
	}

	if ( g_visible )
	{
		if ( g_mouse_click_new )
		{
			g_mouse_click_new = FALSE;
			//if ( remixOnline && remixInterface.pick_RequestObjectPicking )
			//{
			//	const int32_t RECT_SPAN = 3;
			//	remixapi_Rect2D rect;
			//	rect.left = g_mouse_click_pos.x - RECT_SPAN;
			//	rect.right = g_mouse_click_pos.x + RECT_SPAN;
			//	rect.top = g_mouse_click_pos.y - RECT_SPAN;
			//	rect.bottom = g_mouse_click_pos.y + RECT_SPAN;
			//	remixInterface.pick_RequestObjectPicking( &rect, qdx_light_pick_callback, NULL );
			//}
		}

		ImGui_ImplDX9_NewFrame();
		ImGui_ImplWin32_NewFrame();
		ImGui::NewFrame();

		DWORD zen_val = D3DState.EnableState.depthTestEnabled;
		DWORD alpha_val = D3DState.EnableState.alphaBlendEnabled;
		DWORD scis_val = D3DState.EnableState.scissorEnabled;

		if ( zen_val != FALSE )
			D3DState_SetRenderState(D3DRS_ZENABLE, FALSE);
		if ( alpha_val != FALSE )
			D3DState_SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
		if ( scis_val != FALSE )
			D3DState_SetRenderState( D3DRS_SCISSORTESTENABLE, FALSE );

		do_draw();

		if ( D3DGlobal.sceneBegan )
		{
			ImGui::Render();
			ImGui_ImplDX9_RenderDrawData( ImGui::GetDrawData() );
		}
		else
		{
			rmx_console_printf( PRINT_ERROR, "Scene did not begin!\n" );
		}

		if ( zen_val != FALSE )
			D3DState_SetRenderState(D3DRS_ZENABLE, D3DState.EnableState.depthTestEnabled);
		if ( alpha_val != FALSE )
			D3DState_SetRenderState(D3DRS_ALPHABLENDENABLE, D3DState.EnableState.alphaBlendEnabled);
		if ( scis_val != FALSE )
			D3DState_SetRenderState( D3DRS_SCISSORTESTENABLE, D3DState.EnableState.scissorEnabled );
	}
}

static LRESULT CALLBACK wnd_proc_hk(HWND hWnd, UINT message_type, WPARAM wParam, LPARAM lParam)
{
	bool pass_msg_to_game = false;

	switch (message_type)
	{
	case WM_KEYUP: case WM_SYSKEYUP: // always pass button up events to prevent "stuck" game keys

									 // allows user to move the game window via titlebar :>
	case WM_NCLBUTTONDOWN: case WM_NCLBUTTONUP: case WM_NCMOUSEMOVE: case WM_NCMOUSELEAVE:
	case WM_WINDOWPOSCHANGED: //case WM_WINDOWPOSCHANGING:

							  //case WM_INPUT:
							  //case WM_NCHITTEST: // sets cursor to center
							  //case WM_SETCURSOR: case WM_CAPTURECHANGED:

	case WM_NCACTIVATE:
	case WM_SETFOCUS: case WM_KILLFOCUS:
	case WM_SYSCOMMAND:

	case WM_GETMINMAXINFO: case WM_ENTERSIZEMOVE: case WM_EXITSIZEMOVE:
	case WM_SIZING: case WM_MOVING: case WM_MOVE:

	case WM_CLOSE: case WM_ACTIVATE:
		pass_msg_to_game = true;
		break;

	case WM_MOUSEACTIVATE: 
		//if (!io->WantCaptureMouse)
		//{
		//	pass_msg_to_game = true;
		//}
		break;

	default: break; 
	}

	BOOL imgui_consumed = ImGui_ImplWin32_WndProcHandler( hWnd, message_type, wParam, lParam );

	static bool mouse0down = false;
	if ( !imgui_consumed )
	{
		if ( io->KeyCtrl && io->MouseDown[0] && mouse0down == false )
		{
			mouse0down = true;
			g_mouse_click_pos = io->MousePos;
			g_mouse_click_new = TRUE;
		}
	}

	if ( io->MouseDown[0] == false )
	{
		mouse0down = false;
	}

	if ( g_game_wndproc )
	{

		//if ( FALSE == g_game_input_blocked )
		//{
		//	//send mouse too
		//	if ( io->MouseDelta.x || io->MouseDelta.y )
		//	{
		//		Sys_QueEvent( 0, SE_MOUSE, io->MouseDelta.x, io->MouseDelta.y, 0, NULL );
		//	}
		//}

		if ( pass_msg_to_game || ( !imgui_consumed && !io->WantCaptureMouse && !io->WantCaptureKeyboard) )
		{
			return g_game_wndproc( hWnd, message_type, wParam, lParam );
		}
	}

	//return TRUE;
	return DefWindowProc(hWnd, message_type, wParam, lParam);
}

static void exchange_wndproc_and_mouse( enum exc_wprocnmouse_e action )
{
	switch ( action )
	{
	case WNDPROC_SET_IMGUI:
		g_game_wndproc = (WNDPROC)(SetWindowLongPtr(HWND(g_hwnd), GWLP_WNDPROC, LONG_PTR(wnd_proc_hk)));
		g_in_mouse_val = rmx_gamevar_get("in_mouse");
		if ( g_in_mouse_val )
			rmx_gamevar_set( "in_mouse", "0" );
		rmx_deactivate_mouse();
		break;
	case WNDPROC_RESTORE_GAME:
		if( g_game_wndproc )
			SetWindowLongPtr( HWND( g_hwnd ), GWLP_WNDPROC, LONG_PTR( g_game_wndproc ) );
		g_game_wndproc = NULL;
		if( g_in_mouse_val )
			rmx_gamevar_set( "in_mouse", "1" );
		g_in_mouse_val = 0;
		break;
	}
}