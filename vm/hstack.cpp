// hstack <team> [thread-name-substring [scan]]: print every thread's stack as
// image+offset frames by attaching as the team debugger and walking the
// arm64 frame-pointer chain. Haiku arm64 has no Debugger app.
#include <OS.h>
#include <image.h>
#include <debugger.h>
#include <debug_support.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <vector>
#include <string>

struct Image {
	addr_t start, end;
	std::string name;
};

static std::vector<Image> gImages;

static void
print_addr(int idx, void* address)
{
	addr_t a = (addr_t)address;
	for (const Image& img : gImages) {
		if (a >= img.start && a < img.end) {
			const char* base = strrchr(img.name.c_str(), '/');
			printf("  #%-2d %s+0x%lx\n", idx, base ? base + 1 : img.name.c_str(),
				(unsigned long)(a - img.start));
			return;
		}
	}
	printf("  #%-2d 0x%lx\n", idx, (unsigned long)a);
}

int
main(int argc, char** argv)
{
	if (argc < 2) {
		fprintf(stderr, "usage: hstack <team> [thread-name-substring]\n");
		return 1;
	}
	team_id team = atoi(argv[1]);
	const char* filter = argc > 2 ? argv[2] : NULL;
	bool scan = argc > 3 && strcmp(argv[3], "scan") == 0;

	image_info info;
	int32 cookie = 0;
	while (get_next_image_info(team, &cookie, &info) == B_OK) {
		// text is the first segment; symbolize as file offset from it
		gImages.push_back({(addr_t)info.text,
			(addr_t)info.text + info.text_size, info.name});
	}

	port_id port = create_port(100, "hstack");
	port_id nub = install_team_debugger(team, port);
	if (nub < 0) {
		fprintf(stderr, "install_team_debugger: %s\n", strerror(nub));
		return 1;
	}
	debug_context ctx;
	init_debug_context(&ctx, team, nub);

	thread_info tinfo;
	cookie = 0;
	while (get_next_thread_info(team, &cookie, &tinfo) == B_OK) {
		if (filter && !strstr(tinfo.name, filter))
			continue;
		if (debug_thread(tinfo.thread) != B_OK)
			continue;
		// wait until this thread reports in
		for (int tries = 0; tries < 50; tries++) {
			int32 code;
			debug_debugger_message_data msg;
			ssize_t n = read_port_etc(port, &code, &msg, sizeof(msg),
				B_RELATIVE_TIMEOUT, 200000);
			if (n < 0)
				break;
			if (code == B_DEBUGGER_MESSAGE_THREAD_DEBUGGED
				&& msg.origin.thread == tinfo.thread)
				break;
		}
		printf("thread %d \"%s\" state %d\n", (int)tinfo.thread, tinfo.name,
			(int)tinfo.state);
		void* ip;
		void* fp;
		if (debug_get_instruction_pointer(&ctx, tinfo.thread, &ip, &fp) != B_OK) {
			printf("  (no cpu state)\n");
			continue;
		}
		print_addr(0, ip);
		if (scan) {
			debug_cpu_state cpu;
			if (debug_get_cpu_state(&ctx, tinfo.thread, NULL, &cpu) == B_OK) {
				printf("  sp 0x%lx stack [0x%lx, 0x%lx) used %ld KB of %ld KB\n",
					(unsigned long)cpu.sp, (unsigned long)tinfo.stack_base,
					(unsigned long)tinfo.stack_end,
					(long)(((addr_t)tinfo.stack_end - cpu.sp) / 1024),
					(long)(((addr_t)tinfo.stack_end - (addr_t)tinfo.stack_base) / 1024));
				printf("  lr: "); print_addr(-1, (void*)cpu.x[30]);
				addr_t sp = cpu.sp;
				static uint64 words[4096];
				ssize_t n = debug_read_memory_partial(&ctx, (void*)sp, words, sizeof(words));
				for (ssize_t i = 0; i < n / 8; i++) {
					addr_t w = words[i];
					for (const Image& img : gImages) {
						if (w >= img.start && w < img.end) {
							printf("  sp+0x%-5lx", (unsigned long)(i * 8));
							print_addr(-2, (void*)w);
							break;
						}
					}
				}
			}
			continue;
		}
		for (int i = 1; i < 48 && fp != NULL; i++) {
			debug_stack_frame_info frame;
			if (debug_get_stack_frame(&ctx, fp, &frame) != B_OK)
				break;
			if (frame.return_address == NULL)
				break;
			print_addr(i, frame.return_address);
			if ((addr_t)frame.parent_frame <= (addr_t)fp)
				break;
			fp = frame.parent_frame;
		}
	}

	destroy_debug_context(&ctx);
	remove_team_debugger(team);
	return 0;
}
