// std
#include <math.h>
#include <stdio.h>
#include <time.h>
// odin
#include "core.h"
#include "graphics.h"
#include "net.h"
#include "net_msgs.h"
#include "player.h"



struct Client_Globals
{
	Player_Input player_input;
};


Globals* globals;
Client_Globals* client_globals;



static void log_func(const char* format, va_list args)
{
	char buffer[512];
	vsnprintf(buffer, 512, format, args);
	OutputDebugStringA(buffer);
}

// todo(jbr) input thread
static void update_input(WPARAM keycode, bool32 value)
{
	switch (keycode)
	{
		case 'A':
			client_globals->player_input.left = value;
		break;

		case 'D':
			client_globals->player_input.right = value;
		break;

		case 'W':
			client_globals->player_input.up = value;
		break;

		case 'S':
			client_globals->player_input.down = value;
		break;
	}
}

LRESULT CALLBACK WindowProc( HWND window_handle, UINT message, WPARAM w_param, LPARAM l_param )
{
	switch (message)
	{
		case WM_DESTROY:
			PostQuitMessage(0);
			return 0;
		break;

		case WM_KEYDOWN:
			update_input(w_param, 1);
			return 0;
		break;

		case WM_KEYUP:
			update_input(w_param, 0);
			return 0;
		break;
	}

	return DefWindowProc( window_handle, message, w_param, l_param );
}

int CALLBACK WinMain( HINSTANCE instance, HINSTANCE /*prev_instance*/, LPSTR /*cmd_line*/, int cmd_show )
{
	WNDCLASS window_class;
	window_class.style = 0;
	window_class.lpfnWndProc = WindowProc;
	window_class.cbClsExtra = 0;
	window_class.cbWndExtra = 0;
	window_class.hInstance = instance;
	window_class.hIcon = 0;
	window_class.hCursor = 0;
	window_class.hbrBackground = 0;
	window_class.lpszMenuName = 0;
	window_class.lpszClassName = "app_window_class";

	ATOM window_class_atom = RegisterClass( &window_class );

	assert( window_class_atom );


	constexpr uint32 c_window_width = 1280;
	constexpr uint32 c_window_height = 720;

	HWND window_handle;
	{
		LPCSTR 	window_name 	= "";
		DWORD 	style 			= WS_OVERLAPPED;
		int 	x 				= CW_USEDEFAULT;
		int 	y 				= 0;
		HWND 	parent_window 	= 0;
		HMENU 	menu 			= 0;
		LPVOID 	param 			= 0;

		window_handle 			= CreateWindowA( window_class.lpszClassName, window_name, style, x, y, c_window_width, c_window_height, parent_window, menu, instance, param );

		assert( window_handle );
	}
	ShowWindow( window_handle, cmd_show );

	
	// do this before anything else
	globals_init(&log_func);
	client_globals = (Client_Globals*)alloc_permanent(sizeof(Client_Globals));
	client_globals->player_input = {};
	
	// init graphics
	Graphics::State* graphics_state = (Graphics::State*)alloc_permanent(sizeof(Graphics::State));
	Graphics::init(graphics_state, window_handle, instance, 
					c_window_width, c_window_height, c_max_clients);

	if (!Net::init())
	{
		return 0;
	}
	Net::Socket sock;
	if (!Net::socket(&sock))
	{
		return 0;
	}
	Net::socket_set_fake_lag_s(&sock, 0.2f, c_ticks_per_second, c_ticks_per_second, c_packet_budget_per_tick); // 200ms of fake lag

	constexpr uint32 c_socket_buffer_size = c_packet_budget_per_tick;
	uint8* socket_buffer = alloc_permanent(c_socket_buffer_size);
	Net::IP_Endpoint server_endpoint = Net::ip_endpoint(127, 0, 0, 1, c_port);

	uint32 join_msg_size = Net::client_msg_join_write(socket_buffer);
	if (!Net::socket_send(&sock, socket_buffer, join_msg_size, &server_endpoint))
	{
		return 0;
	}


	Player_Snapshot_State* player_snapshot_states = (Player_Snapshot_State*)alloc_permanent(sizeof(Player_Snapshot_State) * c_max_clients);
	bool32* players_present = (bool32*)alloc_permanent(sizeof(bool32) * c_max_clients);
	Matrix_4x4* mvp_matrices = (Matrix_4x4*)alloc_permanent(sizeof(Matrix_4x4) * (c_max_clients + 1));

	Player_Snapshot_State* local_player_snapshot_state = (Player_Snapshot_State*)alloc_permanent(sizeof(Player_Snapshot_State));
	Player_Extra_State* local_player_extra_state = (Player_Extra_State*)alloc_permanent(sizeof(Player_Extra_State));

	constexpr uint32 c_prediction_history_capacity = c_ticks_per_second * 2;
	Player_Snapshot_State* prediction_history_snapshot_state = (Player_Snapshot_State*)alloc_permanent(sizeof(Player_Snapshot_State) * c_prediction_history_capacity);
	Player_Extra_State* prediction_history_extra_state = (Player_Extra_State*)alloc_permanent(sizeof(Player_Extra_State) * c_prediction_history_capacity);
	Player_Input* prediction_history_input = (Player_Input*)alloc_permanent(sizeof(Player_Input) * c_prediction_history_capacity);
	Circular_Index prediction_history_index = circular_index(c_prediction_history_capacity);

	constexpr float32 c_fov_y = 60.0f * c_deg_to_rad;
	constexpr float32 c_aspect_ratio = c_window_width / (float32)c_window_height;
	constexpr float32 c_near_plane = 1.0f;
	constexpr float32 c_far_plane = 100.0f;
	Matrix_4x4 projection_matrix;
	matrix_4x4_projection(&projection_matrix, c_fov_y, c_aspect_ratio, c_near_plane, c_far_plane);

	uint32 local_player_slot = (uint32)-1;
	uint32 tick_number = (uint32)-1;
	uint32 target_tick_number = (uint32)-1;
	Timer local_timer = timer();
	Timer tick_timer = timer();

	
	// main loop
	int exit_code = 0;
	while (true)
	{
		timer_restart(&tick_timer);

		// Windows messages
		bool32 got_quit_message = 0;
		MSG message;
		HWND hwnd = 0; // WM_QUIT is not associated with a window, so this must be 0
		UINT filter_min = 0;
		UINT filter_max = 0;
		UINT remove_message = PM_REMOVE;
		while (PeekMessage(&message, hwnd, filter_min, filter_max, remove_message))
		{
			if (message.message == WM_QUIT)
			{
				exit_code = (int)message.wParam;
				got_quit_message = 1;
				break;
			}
			TranslateMessage( &message );
			DispatchMessage( &message );
		}
		if (got_quit_message)
		{
			break;
		}


		// Process Packets
		uint32 bytes_received;
		Net::IP_Endpoint from;
		while (Net::socket_receive(&sock, socket_buffer, c_socket_buffer_size, &bytes_received, &from))
		{
			switch ((Net::Server_Message)socket_buffer[0])
			{
				case Net::Server_Message::Join_Result:
				{
					bool32 success;
					Net::server_msg_join_result_read(socket_buffer, &success, &local_player_slot);
					if (!success)
					{
						log("[client] server didn't let us in\n");
					}
				}
				break;

				case Net::Server_Message::State:
				{
					uint32 received_tick_number;
					uint32 received_timestamp;
					Player_Extra_State received_local_player_extra_state;
					Net::server_msg_state_read(
						socket_buffer, 
						&received_tick_number, 
						&received_timestamp, 
						&received_local_player_extra_state, 
						player_snapshot_states, 
						players_present,
						c_max_clients);

					uint32 time_now_ms = (uint32)(timer_get_s(&local_timer) * 1000.0f);
					uint32 est_rtt_ms = time_now_ms - received_timestamp;
					
					// predict at tick number of this state packet, plus rtt, plus a bit for jitter
					// todo(jbr) better method of working out how much to predict
					float32 est_rtt_s = est_rtt_ms / 1000.0f;
					uint32 ticks_to_predict = (uint32)(est_rtt_s / c_seconds_per_tick);
					ticks_to_predict += 2;
					target_tick_number = received_tick_number + ticks_to_predict;

					if (tick_number == (uint32)-1 ||
					 	received_tick_number >= tick_number)
					{
						// on first state message, or when the server manages to get ahead of us, just reset our prediction etc from this state message
						*local_player_extra_state = received_local_player_extra_state;
						tick_number = target_tick_number;
					}
					else
					{
						uint32 oldest_predicted_tick_number = tick_number - prediction_history_index.size;
						while (prediction_history_index.size && 
							oldest_predicted_tick_number < received_tick_number)
						{
							// discard this one, not needed
							++oldest_predicted_tick_number;
							circular_index_pop(&prediction_history_index);
						}

						if (prediction_history_index.size &&
							oldest_predicted_tick_number == received_tick_number)
						{
							Player_Snapshot_State* received_local_player_snapshot_state = &player_snapshot_states[local_player_slot];

							float32 dx = prediction_history_snapshot_state[prediction_history_index.head].x - received_local_player_snapshot_state->x;
							float32 dy = prediction_history_snapshot_state[prediction_history_index.head].y - received_local_player_snapshot_state->y;
							constexpr float32 c_max_error = 0.01f;
							constexpr float32 c_max_error_sq = c_max_error * c_max_error;
							float32 error_sq = (dx * dx) + (dy * dy);
							if (error_sq > c_max_error_sq)
							{
								log("[client]error of %f detected at tick %d, rewinding and replaying\n", sqrtf(error_sq), received_tick_number);

								*local_player_snapshot_state = *received_local_player_snapshot_state;
								*local_player_extra_state = received_local_player_extra_state;
								for (uint32 i = 0; i < prediction_history_index.size; ++i)
								{
									uint32 circular_i = circular_index_iterator(&prediction_history_index, i);
									
									prediction_history_snapshot_state[circular_i] = *local_player_snapshot_state;
									prediction_history_extra_state[circular_i] = *local_player_extra_state;

									tick_player(local_player_snapshot_state, local_player_extra_state, &prediction_history_input[circular_i]);
								}
							}
						}
					}
				}
				break;
			}
		}


		// tick player if we have one
		if (local_player_slot != (uint32)-1 && 
			tick_number != (uint32)-1)
		{
			uint32 time_ms = (uint32)(timer_get_s(&local_timer) * 1000.0f);
			uint32 input_msg_size = Net::client_msg_input_write(socket_buffer, local_player_slot, &client_globals->player_input, time_ms, tick_number);
			Net::socket_send(&sock, socket_buffer, input_msg_size, &server_endpoint);

			// todo(jbr) speed up/slow down rather than doing ALL predicted ticks
			while (tick_number < target_tick_number)
			{
				if (circular_index_is_full(&prediction_history_index))
				{
					circular_index_pop(&prediction_history_index);
				}
				uint32 tail = circular_index_tail(&prediction_history_index);
				prediction_history_snapshot_state[tail] = *local_player_snapshot_state;
				prediction_history_extra_state[tail] = *local_player_extra_state;
				prediction_history_input[tail] = client_globals->player_input;
				circular_index_push(&prediction_history_index);

				tick_player(local_player_snapshot_state, local_player_extra_state, &client_globals->player_input);
				++tick_number;
			}

			++target_tick_number;

			// we're always the last player in the array
			player_snapshot_states[local_player_slot] = *local_player_snapshot_state;
		}

		// Create view-projection matrix
		constexpr float32 c_camera_offset_distance = 3.0f;
		Vec_3f camera_pos = vec_3f(
			local_player_snapshot_state->x + (c_camera_offset_distance * sinf(local_player_snapshot_state->facing)), 
			local_player_snapshot_state->y - (c_camera_offset_distance * cosf(local_player_snapshot_state->facing)), 
			1.0f);
		Vec_3f player_pos = vec_3f(local_player_snapshot_state->x, local_player_snapshot_state->y, 0.0f);
		
		Matrix_4x4 view_matrix;
		matrix_4x4_lookat(	&view_matrix, 
							camera_pos, 				// position
							player_pos, 				// target
							vec_3f(0.0f, 0.0f, 1.0f)); 	// up
		
		Matrix_4x4 view_projection_matrix;
		matrix_4x4_mul(&view_projection_matrix, &projection_matrix, &view_matrix);

		// Create mvp matrix for scenery (just copy view-projection, scenery is not moved)
		mvp_matrices[0] = view_projection_matrix;

		// Create mvp matrix for each player
		Matrix_4x4 temp_translation_matrix;
		Matrix_4x4 temp_rotation_matrix;
		
		bool32* players_present_end = &players_present[c_max_clients];
		Player_Snapshot_State* player_snapshot_state = &player_snapshot_states[0];
		Matrix_4x4* player_mvp_matrix = &mvp_matrices[1];
		Matrix_4x4 temp_model_matrix;
		for (bool32* players_present_iter = &players_present[0];
			players_present_iter != players_present_end;
			++players_present_iter, ++player_snapshot_state)
		{
			if (*players_present_iter)
			{
				matrix_4x4_rotation_z(&temp_rotation_matrix, player_snapshot_state->facing);
				matrix_4x4_translation(&temp_translation_matrix, player_snapshot_state->x, player_snapshot_state->y, 0.0f);
				matrix_4x4_mul(&temp_model_matrix, &temp_translation_matrix, &temp_rotation_matrix);
				matrix_4x4_mul(player_mvp_matrix, &view_projection_matrix, &temp_model_matrix);
				
				++player_mvp_matrix;
			}
		}
		uint32 num_matrices = (uint32)(player_mvp_matrix - &mvp_matrices[1]);
		Graphics::update_and_draw(graphics_state, mvp_matrices, num_matrices);


		timer_wait_until(&tick_timer, c_seconds_per_tick);
	}

	uint32 leave_msg_size = Net::client_msg_leave_write(socket_buffer, local_player_slot);
	Net::socket_send(&sock, socket_buffer, leave_msg_size, &server_endpoint);
	Net::socket_close(&sock);

	return exit_code;
}