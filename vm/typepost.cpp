// typepost <team> <title-substring> <text>: activate that team's window and
// type <text> into its focus view. "\n" in <text> presses Enter.
#include <Application.h>
#include <InterfaceDefs.h>
#include <Message.h>
#include <Messenger.h>
#include <MessengerPrivate.h>
#include <OS.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char** argv)
{
	if (argc < 4) {
		fprintf(stderr, "usage: typepost <team> <title-substring> <text>\n");
		return 2;
	}
	BApplication app("application/x-vnd.bluevisor-typepost");
	BMessenger target(NULL, atoi(argv[1]));
	if (!target.IsValid()) {
		fprintf(stderr, "no such team\n");
		return 1;
	}
	int index = -1;
	for (int i = 0; i < 32 && index < 0; i++) {
		BMessage get(B_GET_PROPERTY), reply;
		get.AddSpecifier("Title");
		get.AddSpecifier("Window", (int32)i);
		const char* title;
		if (target.SendMessage(&get, &reply, 2000000, 2000000) != B_OK
			|| reply.FindString("result", &title) != B_OK)
			break;
		printf("window %d: %s\n", i, title);
		if (strstr(title, argv[2]) != NULL)
			index = i;
	}
	if (index < 0) {
		fprintf(stderr, "no window matching \"%s\"\n", argv[2]);
		return 1;
	}

	BMessage activate(B_SET_PROPERTY), ignored;
	activate.AddSpecifier("Active");
	activate.AddSpecifier("Window", (int32)index);
	activate.AddBool("data", true);
	target.SendMessage(&activate, &ignored, 2000000, 2000000);
	snooze(500000);

	// Messages sent through the application land on the window handler, which
	// ignores keys; the preferred token routes them to the focus view instead.
	BMessage getMessenger(B_GET_PROPERTY), messengerReply;
	getMessenger.AddSpecifier("Messenger");
	getMessenger.AddSpecifier("Window", (int32)index);
	BMessenger window;
	if (target.SendMessage(&getMessenger, &messengerReply, 2000000, 2000000) != B_OK
		|| messengerReply.FindMessenger("result", &window) != B_OK) {
		fprintf(stderr, "could not get the window's messenger\n");
		return 1;
	}
	BMessenger::Private(window).SetTo(BMessenger::Private(window).Team(),
		BMessenger::Private(window).Port(), B_PREFERRED_TOKEN);

	const uint32 whats[] = {B_KEY_DOWN, B_KEY_UP};
	for (const char* c = argv[3]; *c; c++) {
		char ch = *c;
		int32 keycode = 0;
		if (ch == '\\' && c[1] == 'n') {
			ch = B_ENTER;
			keycode = 0x47;
			c++;
		}
		char bytes[2] = {ch, 0};
		for (int w = 0; w < 2; w++) {
			BMessage key(whats[w]);
			key.AddInt64("when", system_time());
			key.AddInt32("modifiers", 0);
			key.AddInt32("key", keycode);
			key.AddInt32("raw_char", ch);
			key.AddInt8("byte", ch);
			key.AddString("bytes", bytes);
			window.SendMessage(&key);
			snooze(30000);
		}
	}
	printf("typed into window %d\n", index);
	return 0;
}
