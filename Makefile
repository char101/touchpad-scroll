all: touchpad-scroll.exe

touchpad-scroll.exe: main.cpp resource.h app.rc app.ico
	rc /nologo app.rc
	cl /nologo /c /O2 /GS- /GL /MD /EHsc /DUNICODE main.cpp
	link /opt:ref /opt:icf /debug:none /ltcg /out:$@ main.obj app.res user32.lib comctl32.lib gdi32.lib shell32.lib
	del *.obj
