// Render bridges for GTA2_HEADLESS builds (jalon 1 and soak tests without a window): the
// draw list is built as usual (port/render/frame.cpp) and dropped at present.
// Signatures: see port/native/platform/js_bridge_null.cpp.
#if defined(GTA2_NATIVE_HEADLESS)
extern "C" {

void gta2_js_present(const void*, int, const void*, int, int)
{
}
void gta2_js_upload_texture(int, int, int, const void*)
{
}
void gta2_js_free_texture(int)
{
}
void gta2_js_free_all_textures()
{
}

} // extern "C"
#endif
