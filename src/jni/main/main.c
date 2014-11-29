#include "SDL.h"
#include "SDL_image.h"
#include "Python.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <jni.h>
#include "android/log.h"
#include "jniwrapperstuff.h"

SDL_Window *window = NULL;


#define LOG(x) __android_log_write(ANDROID_LOG_INFO, "python", (x))

static PyObject *androidembed_log(PyObject *self, PyObject *args) {
    char *logstr = NULL;
    if (!PyArg_ParseTuple(args, "s", &logstr)) {
        return NULL;
    }
    LOG(logstr);
    Py_RETURN_NONE;
}

static PyObject *androidembed_close_window(PyObject *self, PyObject *args) {
    char *logstr = NULL;
    if (!PyArg_ParseTuple(args, "")) {
        return NULL;
    }

    if (window) {
		SDL_DestroyWindow(window);
		window = NULL;
    }

    Py_RETURN_NONE;
}

static PyMethodDef AndroidEmbedMethods[] = {
	    {"log", androidembed_log, METH_VARARGS, "Log on android platform."},
	    {"close_window", androidembed_close_window, METH_VARARGS, "Close the initial window."},
    {NULL, NULL, 0, NULL}
};

PyMODINIT_FUNC initandroidembed(void) {
    (void) Py_InitModule("androidembed", AndroidEmbedMethods);
}

int file_exists(const char * filename) {
	FILE *file;
    if (file = fopen(filename, "r")) {
        fclose(file);
        return 1;
    }
    return 0;
}


int start_python(void) {
    char *env_argument = NULL;
    int ret = 0;
    FILE *fd;
    char *args[] = { "python", NULL };

    LOG("Initialize Python for Android");

    env_argument = getenv("ANDROID_ARGUMENT");
    setenv("ANDROID_APP_PATH", env_argument, 1);

    //setenv("PYTHONVERBOSE", "2", 1);
    Py_SetProgramName(args[0]);
    Py_Initialize();
    PySys_SetArgvEx(1, args, 0);

    /* ensure threads will work.
     */
    PyEval_InitThreads();

    /* our logging module for android
     */
    initandroidembed();

    /* inject our bootstrap code to redirect python stdin/stdout
     * replace sys.path with our path
     */
    PyRun_SimpleString(
        "import sys, posix\n" \
        "private = posix.environ['ANDROID_PRIVATE']\n" \
        "argument = posix.environ['ANDROID_ARGUMENT']\n" \
        "sys.path[:] = [ \n" \
		"    argument, \n" \
		"    private + '/lib/python27.zip', \n" \
		"    private + '/lib/python2.7/', \n" \
		"    private + '/lib/python2.7/lib-dynload/', \n" \
		"    private + '/lib/python2.7/site-packages/', \n" \
		"    ]\n" \
        "import androidembed\n" \
        "class LogFile(object):\n" \
        "    def __init__(self):\n" \
        "        self.buffer = ''\n" \
        "    def write(self, s):\n" \
        "        s = self.buffer + s\n" \
        "        lines = s.split(\"\\n\")\n" \
        "        for l in lines[:-1]:\n" \
        "            androidembed.log(l)\n" \
        "        self.buffer = lines[-1]\n" \
        "    def flush(self):\n" \
        "        return\n" \
        "sys.stdout = sys.stderr = LogFile()\n" \
		"import site; print site.getsitepackages()\n"\
		"print '3...'\n"\
		"print '2...'\n"\
		"print '1...'\n"\
		"print 'Android path', sys.path\n" \
        "print 'Android bootstrap done. __name__ is', __name__\n"\
		"import pygame_sdl2\n"\
		"pygame_sdl2.import_as_pygame()\n"\
    	"");

    /* run it !
     */
    LOG("Run user program, change dir and execute main.py");
    chdir(env_argument);

	/* search the initial main.py
	 */
	char *main_py = "main.pyo";
	if ( file_exists(main_py) == 0 ) {
		if ( file_exists("main.py") )
			main_py = "main.py";
		else
			main_py = NULL;
	}

	if ( main_py == NULL ) {
		LOG("No main.pyo / main.py found.");
		return 1;
	}

    fd = fopen(main_py, "r");
    if ( fd == NULL ) {
        LOG("Open the main.py(o) failed");
        return 1;
    }

    /* run python !
     */
    ret = PyRun_SimpleFile(fd, main_py);

    if (PyErr_Occurred() != NULL) {
        ret = 1;
        PyErr_Print(); /* This exits with the right code if SystemExit. */
        if (Py_FlushLine())
			PyErr_Clear();
    }

    /* close everything
     */
	Py_Finalize();
    fclose(fd);

    LOG("Python for android ended.");
    return ret;
}


JNIEXPORT void JNICALL JAVA_EXPORT_NAME(PythonSDLActivity_nativeSetEnv) (
		JNIEnv*  env, jobject thiz,
		jstring variable,
		jstring value) {

	jboolean iscopy;
    const char *c_variable = (*env)->GetStringUTFChars(env, variable, &iscopy);
    const char *c_value  = (*env)->GetStringUTFChars(env, value, &iscopy);
    setenv(c_variable, c_value, 1);
}

void call_prepare_python(void) {
	JNIEnv* env = (JNIEnv*) SDL_AndroidGetJNIEnv();
	jobject activity = (jobject) SDL_AndroidGetActivity();
	jclass clazz = (*env)->GetObjectClass(env, activity);
	jmethodID method_id = (*env)->GetMethodID(env, clazz, "preparePython", "()V");
	(*env)->CallVoidMethod(env, activity, method_id);
	(*env)->DeleteLocalRef(env, activity);
	(*env)->DeleteLocalRef(env, clazz);
}

Uint32 getpixel(SDL_Surface *surface, int x, int y) {
    int bpp = surface->format->BytesPerPixel;

    Uint8 *p = (Uint8 *)surface->pixels + y * surface->pitch + x * bpp;

    switch(bpp) {
    case 1:
        return *p;
        break;

    case 2:
        return *(Uint16 *)p;
        break;

    case 3:
        if(SDL_BYTEORDER == SDL_BIG_ENDIAN)
            return p[0] << 16 | p[1] << 8 | p[2];
        else
            return p[0] | p[1] << 8 | p[2] << 16;
        break;

    case 4:
        return *(Uint32 *)p;
        break;

    default:
        return 0;       /* shouldn't happen, but avoids warnings */
    }
}

int SDL_main(int argc, char **argv) {
	SDL_Surface *surface;
	SDL_RWops *rwops = NULL;
	SDL_Surface *presplash = NULL;;
	SDL_Rect pos;
	Uint32 pixel;

	int display_width = 1024;
	int display_height = 768;
	SDL_DisplayMode mode;

	if (SDL_Init(SDL_INIT_EVERYTHING) < 0) {
		return 1;
	}

	IMG_Init(IMG_INIT_JPG|IMG_INIT_PNG);

	if (SDL_GetCurrentDisplayMode(0, &mode) == 0) {
		display_width = mode.w;
		display_height = mode.h;
	}

	window = SDL_CreateWindow("pygame_sdl2 starting...", 0, 0, display_width, display_height, SDL_WINDOW_SHOWN);
	surface = SDL_GetWindowSurface(window);
	pixel = SDL_MapRGB(surface->format, 128, 128, 128);

	rwops = SDL_RWFromFile("android-presplash.jpg", "r");
	if (!rwops) goto done;

	presplash = IMG_Load_RW(rwops, 1);
	if (!presplash) goto done;

	pixel = getpixel(presplash, 0, 0);

done:

	SDL_FillRect(surface, NULL, pixel);

	if (presplash) {
		pos.x = (display_width - presplash->w) / 2;
		pos.y = (display_height - presplash->h) / 2;
		SDL_BlitSurface(presplash, NULL, surface, &pos);
		SDL_FreeSurface(presplash);
	}

	SDL_UpdateWindowSurface(window);

	call_prepare_python();

	return start_python();
}