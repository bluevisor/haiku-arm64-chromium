// Exit 0 when Haiku's UI uses a dark panel color (Appearance > Colors), so
// the R Chromium launcher can render pages dark to match.
#include <Application.h>
#include <InterfaceDefs.h>

int main()
{
	BApplication app("application/x-vnd.rchromium-ui-is-dark");
	return ui_color(B_PANEL_BACKGROUND_COLOR).IsDark() ? 0 : 1;
}
