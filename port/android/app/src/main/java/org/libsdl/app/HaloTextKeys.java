package org.libsdl.app;

/**
 * The game's text bar's way into SDL's keyboard (com.halo.decomp.TextBar): a
 * character typed as a key press, as SDL's own input connection types one
 * (SDLInputConnection is not public, so this is in its package). '\b' is a
 * backspace and '\n' the Return key; a character with no key on the layout
 * gets one of SDL's spare scancodes.
 */
public final class HaloTextKeys {
    private HaloTextKeys() {
    }

    public static void type(char c) {
        SDLInputConnection.nativeGenerateScancodeForUnichar(c);
    }
}
