package com.halo.decomp;

import android.content.Context;
import android.graphics.Color;
import android.graphics.Typeface;
import android.graphics.drawable.GradientDrawable;
import android.os.Build;
import android.text.Editable;
import android.text.InputFilter;
import android.text.InputType;
import android.text.TextWatcher;
import android.util.TypedValue;
import android.view.Gravity;
import android.view.KeyEvent;
import android.view.View;
import android.view.WindowInsets;
import android.view.inputmethod.EditorInfo;
import android.view.inputmethod.InputConnection;
import android.view.inputmethod.InputConnectionWrapper;
import android.view.inputmethod.InputMethodManager;
import android.widget.EditText;
import android.widget.LinearLayout;
import android.widget.RelativeLayout;
import android.widget.TextView;

import java.text.Normalizer;

import org.libsdl.app.HaloTextKeys;
import org.libsdl.app.SDLActivity;

/**
 * The text bar: while the game takes typed text (a chat line, a name, a
 * menu's text field: host_sdl_text_input, port/android/host/host_sdl.c), a real
 * text field over the game, docked above Android's keyboard (at the screen's
 * top until the keyboard's height is known), so that what is typed is seen
 * where the keyboard does not cover it, with the keyboard's suggestions,
 * autocorrection, gesture and voice typing and paste.
 *
 * The game still gets keys, as from SDL's own hidden field: each change of
 * the bar's text goes to it as the backspaces and key presses that make its
 * line match (sendDifference), and the keyboard's Send, or the bar's, is the
 * Return key. The game's font has ASCII alone, so the game gets the text with
 * accents taken off and anything else left out (gameText). The bar starts
 * empty; a backspace in it while it is empty is still the game's (a name's
 * old text).
 */
final class TextBar extends LinearLayout {
    /** SDL_TextInputType's SDL_TEXTINPUT_TYPE_TEXT_PASSWORD_HIDDEN (SDL_keyboard.h) */
    static final int TYPE_PASSWORD_HIDDEN = 4;
    /** the longest text the bar takes (the game's own lines are shorter: it drops what is past their end) */
    private static final int MAXIMUM_LENGTH = 255;

    private final Field field;
    private final View send;
    /** what the game has been sent of the bar's text (gameText's) */
    private String sent = "";
    /** set while the bar's text is changed for itself, not typed: nothing goes to the game */
    private boolean quiet;
    /** the keyboard's height over the window's bottom, 0 while unknown or down */
    private int keyboardHeight;
    /** the keyboard has been seen up since the bar was shown (its going then closes the bar) */
    private boolean keyboardSeen;
    /** called once the bar is closed by hand (back, or the keyboard put away) */
    private final Runnable onClosed;

    /** the text field: a backspace at its start, and back, are the bar's */
    private final class Field extends EditText {
        Field(Context context) {
            super(context);
        }

        @Override
        public InputConnection onCreateInputConnection(EditorInfo info) {
            InputConnection connection = super.onCreateInputConnection(info);

            info.imeOptions |= EditorInfo.IME_FLAG_NO_EXTRACT_UI | EditorInfo.IME_FLAG_NO_FULLSCREEN;
            if (connection == null)
                return null;
            return new InputConnectionWrapper(connection, true) {
                @Override
                public boolean deleteSurroundingText(int beforeLength, int afterLength) {
                    if (beforeLength > 0 && afterLength == 0 && atStart()) {
                        HaloTextKeys.type('\b');
                        return true;
                    }
                    return super.deleteSurroundingText(beforeLength, afterLength);
                }

                @Override
                public boolean sendKeyEvent(KeyEvent event) {
                    if (event.getKeyCode() == KeyEvent.KEYCODE_DEL && atStart()) {
                        if (event.getAction() == KeyEvent.ACTION_DOWN)
                            HaloTextKeys.type('\b');
                        return true;
                    }
                    return super.sendKeyEvent(event);
                }
            };
        }

        private boolean atStart() {
            return getSelectionStart() == 0 && getSelectionEnd() == 0;
        }

        @Override
        public boolean onKeyPreIme(int keyCode, KeyEvent event) {
            if (keyCode == KeyEvent.KEYCODE_BACK && event.getAction() == KeyEvent.ACTION_UP
                    && getVisibility() == View.VISIBLE) {
                closedByHand();
                return true;
            }
            return super.onKeyPreIme(keyCode, event);
        }
    }

    TextBar(Context context, Runnable onClosed) {
        super(context);
        this.onClosed = onClosed;
        float density = getResources().getDisplayMetrics().density;
        int padding = (int) (6 * density);

        setOrientation(HORIZONTAL);
        setGravity(Gravity.CENTER_VERTICAL);
        setPadding(padding, padding, padding, padding);
        GradientDrawable background = new GradientDrawable();
        background.setColor(Color.argb(230, 12, 18, 28));
        background.setStroke((int) Math.max(1, density), Color.argb(255, 70, 110, 160));
        background.setCornerRadius(4 * density);
        setBackground(background);

        field = new Field(context);
        field.setSingleLine(true);
        field.setTextColor(Color.WHITE);
        field.setHintTextColor(Color.argb(140, 255, 255, 255));
        field.setTextSize(TypedValue.COMPLEX_UNIT_SP, 18);
        field.setBackgroundColor(Color.TRANSPARENT);
        field.setPadding(padding, 0, padding, 0);
        field.setImeOptions(EditorInfo.IME_ACTION_SEND | EditorInfo.IME_FLAG_NO_EXTRACT_UI
                | EditorInfo.IME_FLAG_NO_FULLSCREEN);
        field.setFilters(new InputFilter[] { new InputFilter.LengthFilter(MAXIMUM_LENGTH) });
        field.addTextChangedListener(new TextWatcher() {
            @Override
            public void beforeTextChanged(CharSequence text, int start, int count, int after) {
            }

            @Override
            public void onTextChanged(CharSequence text, int start, int before, int count) {
            }

            @Override
            public void afterTextChanged(Editable text) {
                if (!quiet)
                    sendDifference(text.toString());
            }
        });
        field.setOnEditorActionListener((view, action, event) -> {
            if (action == EditorInfo.IME_ACTION_SEND || action == EditorInfo.IME_ACTION_DONE
                    || (event != null && event.getKeyCode() == KeyEvent.KEYCODE_ENTER)) {
                if (event == null || event.getAction() == KeyEvent.ACTION_DOWN)
                    submit();
                return true;
            }
            return false;
        });
        // a controller's buttons still go to the game (SDL's surface, which has them while it has the focus)
        field.setOnKeyListener((view, keyCode, event) -> {
            if (keyCode == KeyEvent.KEYCODE_BACK)
                return false;
            if (org.libsdl.app.SDLControllerManager.isDeviceSDLJoystick(event.getDeviceId()))
                return SDLActivity.handleKeyEvent(view, keyCode, event, null);
            return false;
        });
        addView(field, new LayoutParams(0, LayoutParams.WRAP_CONTENT, 1.0f));

        TextView sendButton = new TextView(context);
        sendButton.setText("SEND");
        sendButton.setTextColor(Color.argb(255, 140, 190, 255));
        sendButton.setTypeface(Typeface.DEFAULT_BOLD);
        sendButton.setTextSize(TypedValue.COMPLEX_UNIT_SP, 16);
        sendButton.setPadding(padding * 2, padding, padding * 2, padding);
        sendButton.setOnClickListener(view -> submit());
        send = sendButton;
        addView(send, new LayoutParams(LayoutParams.WRAP_CONTENT, LayoutParams.WRAP_CONTENT));

        setVisibility(View.GONE);
    }

    /** the game's text of the bar's: ASCII from space to '~', accents taken off, the rest left out */
    static String gameText(String text) {
        StringBuilder result = new StringBuilder(text.length());

        for (int offset = 0; offset < text.length(); ) {
            int codePoint = text.codePointAt(offset);
            offset += Character.charCount(codePoint);
            if (codePoint >= 0x20 && codePoint < 0x7f) {
                result.append((char) codePoint);
                continue;
            }
            if (codePoint < 0x80)
                continue;
            String bare = Normalizer.normalize(new String(Character.toChars(codePoint)), Normalizer.Form.NFD);
            for (int index = 0; index < bare.length(); index++) {
                char c = bare.charAt(index);
                if (c >= 0x20 && c < 0x7f)
                    result.append(c);
            }
        }
        return result.toString();
    }

    /** the game's line made the bar's: backspaces over what differs, then the rest typed */
    private void sendDifference(String text) {
        String wanted = gameText(text);
        int match = 0;
        int limit = Math.min(wanted.length(), sent.length());

        while (match < limit && wanted.charAt(match) == sent.charAt(match))
            match++;
        for (int index = sent.length(); index > match; index--)
            HaloTextKeys.type('\b');
        for (int index = match; index < wanted.length(); index++)
            HaloTextKeys.type(wanted.charAt(index));
        sent = wanted;
    }

    /** Send: the Return key, and the bar emptied for a next line (it closes as the game's typing ends) */
    private void submit() {
        if (getVisibility() != View.VISIBLE)
            return;
        sendDifference(field.getText().toString());
        HaloTextKeys.type('\n');
        clear();
    }

    private void clear() {
        quiet = true;
        field.setText("");
        quiet = false;
        sent = "";
    }

    /** shown, empty, for the SDL_TextInputType given (a hidden password's dots, or text with suggestions) */
    void show(int type) {
        clear();
        if (type == TYPE_PASSWORD_HIDDEN) {
            field.setInputType(InputType.TYPE_CLASS_TEXT | InputType.TYPE_TEXT_VARIATION_PASSWORD);
            field.setHint("Password");
        } else {
            field.setInputType(InputType.TYPE_CLASS_TEXT | InputType.TYPE_TEXT_FLAG_AUTO_CORRECT
                    | InputType.TYPE_TEXT_FLAG_CAP_SENTENCES);
            field.setHint("Type here");
        }
        field.setTypeface(Typeface.DEFAULT);
        // (up already when the game opens its field again at once: its going still closes the bar)
        keyboardSeen = false;
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
            WindowInsets insets = getRootWindowInsets();
            keyboardSeen = insets != null && insets.isVisible(WindowInsets.Type.ime());
        }
        place();
        setVisibility(View.VISIBLE);
        field.requestFocus();
        InputMethodManager manager = (InputMethodManager) getContext().getSystemService(Context.INPUT_METHOD_SERVICE);
        if (manager != null) {
            // (the keyboard is asked for again once the field is laid out, as a first show can come too soon)
            manager.restartInput(field);
            manager.showSoftInput(field, 0);
            post(() -> {
                if (getVisibility() == View.VISIBLE)
                    manager.showSoftInput(field, 0);
            });
        }
    }

    /** gone, with the keyboard */
    void hide() {
        if (getVisibility() != View.VISIBLE)
            return;
        InputMethodManager manager = (InputMethodManager) getContext().getSystemService(Context.INPUT_METHOD_SERVICE);
        if (manager != null)
            manager.hideSoftInputFromWindow(field.getWindowToken(), 0);
        field.clearFocus();
        setVisibility(View.GONE);
        clear();
    }

    private void closedByHand() {
        hide();
        onClosed.run();
    }

    /** the window's insets (HaloActivity's listener): the keyboard's height, Android 11 on */
    void onInsets(WindowInsets insets) {
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.R)
            return;
        boolean up = insets.isVisible(WindowInsets.Type.ime());
        int height = up ? insets.getInsets(WindowInsets.Type.ime()).bottom : 0;

        if (height != keyboardHeight) {
            keyboardHeight = height;
            place();
        }
        if (up)
            keyboardSeen = true;
        else if (keyboardSeen && getVisibility() == View.VISIBLE)
            post(this::closedByHand);
    }

    /** across the screen, above the keyboard when its height is known, else at the top */
    private void place() {
        float density = getResources().getDisplayMetrics().density;
        int margin = (int) (8 * density);
        RelativeLayout.LayoutParams params = new RelativeLayout.LayoutParams(
                RelativeLayout.LayoutParams.MATCH_PARENT, RelativeLayout.LayoutParams.WRAP_CONTENT);

        params.leftMargin = margin;
        params.rightMargin = margin;
        if (keyboardHeight > 0) {
            params.addRule(RelativeLayout.ALIGN_PARENT_BOTTOM);
            params.bottomMargin = keyboardHeight + margin / 2;
        } else {
            params.addRule(RelativeLayout.ALIGN_PARENT_TOP);
            params.topMargin = margin;
        }
        setLayoutParams(params);
    }
}
