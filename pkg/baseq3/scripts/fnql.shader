// FnQL-owned compatibility aliases. Referenced images remain external retail
// Quake Live assets and are never redistributed in this package.
fnql/console
{
	nopicmip
	nomipmaps
	{
		map textures/sfx2/screen01.tga
		blendfunc GL_ONE GL_ZERO
		tcMod scroll 7.1 0.2
		tcmod scale .8 1
	}
	{
		map textures/effects2/console01.tga
		blendfunc add
		tcMod scroll -.01 -.02
		tcmod scale .02 .01
		tcmod rotate 3
	}
}
