#include <xosd.h>
#include <stdlib.h>

int main (int argc, char *argv[])
{
	xosd *osd;
	osd = xosd_create (1);

	xosd_set_font(osd, "mix");
	xosd_set_colour(osd, "LawnGreen");
	xosd_set_shadow_offset(osd, 1);
	//xosd_set_timeout(osd, 3);

	xosd_display (osd, 0, XOSD_string, "Example XOSD output");

	xosd_wait_until_no_display(osd);

	xosd_destroy (osd);

	return EXIT_SUCCESS;
}

