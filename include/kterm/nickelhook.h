#ifndef KTERM_NICKELHOOK_H
#define KTERM_NICKELHOOK_H

#include <stddef.h>
#include <stdbool.h>
#include <stdarg.h>

/*
 * Internal logging function — writes to /mnt/onboard/.adds/koboterminal/koboterminal.log
 * Usage: kterm_log("[koboterminal] message\n");
 */
void kterm_log(const char *fmt, ...);

/*
 * NickelHook integration for KoboTerminal.
 *
 * This header defines the nh_info/nh_hook/nh_dlsym structures used by
 * NickelHook to load shared libraries into the Nickel process.
 *
 * References:
 *   - pgaskin/NickelHook: https://github.com/pgaskin/NickelHook
 *   - shermp/NickelDBus:  https://github.com/shermp/NickelDBus
 *
 * Key Nickel C++ classes (mangled symbol names):
 *   ConfirmationDialogFactory    _ZN25ConfirmationDialogFactory...
 *   ConfirmationDialog           _ZN18ConfirmationDialog...
 *   N3ConfirmationTextEditField  _ZN27N3ConfirmationTextEditField...
 *   TouchLineEdit                (subclass of QLineEdit)
 */

/* ============================================================================
 * NickelHook API structures (from NickelHook.h)
 * ============================================================================ */

/*
 * nh_info: metadata for the module.
 * Required fields: name (unique identifier)
 * Optional fields: desc, uninstall_flag, uninstall_xflag, failsafe_delay
 */
struct nh_info {
    const char *name;              /* required: unique mod name */
    const char *desc;              /* optional: human-readable description */
    const char *uninstall_flag;    /* optional: path to flag file triggering uninstall */
    const char *uninstall_xflag;   /* optional: path to xflag file triggering uninstall on delete */
    int         failsafe_delay;    /* optional: delay in seconds before disarming failsafe */
};

/*
 * nh_hook: PLT symbol hook definition.
 * Used to intercept calls to Nickel C++ methods.
 */
struct nh_hook {
    const char *sym;     /* the mangled C++ symbol to hook */
    const char *sym_new; /* the replacement symbol in your mod */
    const char *lib;     /* the library to hook (e.g., "libnickel.so.1.0.0") */
    void      **out;     /* variable to store the original function pointer */
    const char *desc;    /* optional: description */
    bool       optional; /* default: false */
};

/*
 * nh_dlsym: symbol resolution definition.
 * Used to resolve Nickel C++ class methods via dlsym.
 */
struct nh_dlsym {
    const char *name;    /* symbol name to resolve with RTLD_DEFAULT */
    void      **out;     /* variable to store the resolved pointer */
    const char *desc;    /* optional */
    bool       optional; /* default: false */
};

/*
 * nh: main NickelHook structure.
 * The NickelHook() macro constructs this with __attribute__((constructor)).
 */
struct nh {
    int (*init)();           /* optional init function */
    struct nh_info  *info;
    struct nh_hook  *hook;   /* NULL-terminated array */
    struct nh_dlsym *dlsym;  /* NULL-terminated array */
    bool (*uninstall)();     /* optional cleanup on uninstall */
};

/*
 * Convenience macro for building nh_hook arrays.
 */
#define nh_symoutptr(sym) (void**)(&(sym))

/*
 * NickelHook() macro: constructs the nh structure with visibility("default").
 * This is the entry point that NickelHook's loader finds via dlsym.
 * Uses static const to avoid -Wpedantic warnings about non-constant initializers.
 */
#define NickelHook(...)                                                    \
    static const struct nh NickelHook_data = {                             \
        __VA_ARGS__                                                        \
    };                                                                    \
    __attribute__((visibility("default")))                                 \
    struct nh NickelHook = NickelHook_data;

/* ============================================================================
 * Nickel C++ class method signatures (from NickelDBus NDBCfmDlg)
 * ============================================================================ */

/*
 * Type aliases for Nickel Qt classes.
 * These are typedef'd in NickelDBus for convenience.
 */
typedef void *ConfirmationDialog;    /* QDialog subclass */
typedef int   KeyboardScript;        /* enum */
typedef void *TouchLineEdit;         /* QLineEdit subclass */
typedef void *N3ConfirmationTextEditField; /* QWidget subclass */

/*
 * Function pointer types for Nickel C++ methods.
 * These are resolved via dlsym at runtime.
 */

/* ConfirmationDialogFactory methods */
typedef ConfirmationDialog *(*ConfirmationDialogFactory_getConfirmationDialog_t)(void *parent);
typedef void                  (*ConfirmationDialogFactory_showTextEditDialog_t)(const void *title);

/* ConfirmationDialog methods (static methods on the class) */
typedef void (*ConfirmationDialog__setTitle_t)(ConfirmationDialog *_this, const void *title);
typedef void (*ConfirmationDialog__setText_t)(ConfirmationDialog *_this, const void *text);
typedef void (*ConfirmationDialog__setAcceptButtonText_t)(ConfirmationDialog *_this, const void *text);
typedef void (*ConfirmationDialog__setRejectButtonText_t)(ConfirmationDialog *_this, const void *text);
typedef void (*ConfirmationDialog__showCloseButton_t)(ConfirmationDialog *_this, bool show);
typedef void (*ConfirmationDialog__setRejectOnOutsideTap_t)(ConfirmationDialog *_this, bool set);
typedef void (*ConfirmationDialog__addWidget_t)(ConfirmationDialog *_this, void *widget);
typedef void (*ConfirmationDialog__show_t)(ConfirmationDialog *_this);
typedef void (*ConfirmationDialog__close_t)(ConfirmationDialog *_this);

/* N3ConfirmationTextEditField constructors */
typedef N3ConfirmationTextEditField *(*N3ConfirmationTextEditField_ctor_t)(
    N3ConfirmationTextEditField *_this, ConfirmationDialog *dlg, KeyboardScript ks);
typedef N3ConfirmationTextEditField *(*N3ConfirmationTextEditField_ctor2_t)(
    N3ConfirmationTextEditField *_this, ConfirmationDialog *dlg);

/* TouchLineEdit accessors */
typedef TouchLineEdit *(*N3ConfirmationTextEditField_textEdit_t)(N3ConfirmationTextEditField *_this);

/* TouchLineEdit methods */
typedef void (*TouchLineEdit__setPlaceholderText_t)(TouchLineEdit *_this, const void *text);
typedef void (*TouchLineEdit__setText_t)(TouchLineEdit *_this, const void *text);
typedef void (*TouchLineEdit__setFocus_t)(TouchLineEdit *_this);
typedef const char *(*TouchLineEdit__text_t)(TouchLineEdit *_this);

/* ============================================================================
 * Resolved Nickel C++ method pointers (defined in nickelhook.c)
 * ============================================================================ */

/* ConfirmationDialogFactory */
extern ConfirmationDialogFactory_getConfirmationDialog_t ConfirmationDialogFactory_getConfirmationDialog;

/* ConfirmationDialog methods */
extern ConfirmationDialog__setTitle_t ConfirmationDialog__setTitle;
extern ConfirmationDialog__setText_t ConfirmationDialog__setText;
extern ConfirmationDialog__setAcceptButtonText_t ConfirmationDialog__setAcceptButtonText;
extern ConfirmationDialog__setRejectButtonText_t ConfirmationDialog__setRejectButtonText;
extern ConfirmationDialog__showCloseButton_t ConfirmationDialog__showCloseButton;
extern ConfirmationDialog__setRejectOnOutsideTap_t ConfirmationDialog__setRejectOnOutsideTap;
extern ConfirmationDialog__addWidget_t ConfirmationDialog__addWidget;
extern ConfirmationDialog__show_t ConfirmationDialog__show;
extern ConfirmationDialog__close_t ConfirmationDialog__close;

/* N3ConfirmationTextEditField constructors */
extern N3ConfirmationTextEditField_ctor_t N3ConfirmationTextEditField_ctor;
extern N3ConfirmationTextEditField_ctor2_t N3ConfirmationTextEditField_ctor2;
extern N3ConfirmationTextEditField_textEdit_t N3ConfirmationTextEditField_textEdit;

/* TouchLineEdit methods */
extern TouchLineEdit__setPlaceholderText_t TouchLineEdit__setPlaceholderText;
extern TouchLineEdit__setText_t TouchLineEdit__setText;
extern TouchLineEdit__setFocus_t TouchLineEdit__setFocus;
extern TouchLineEdit__text_t TouchLineEdit__text;

/* Dialog and input field instances */
extern ConfirmationDialog *kterm_dialog;
extern N3ConfirmationTextEditField *kterm_input_field;
extern int kterm_dialog_visible;

/*
 * Initialize the NickelHook module.
 * Resolves all Nickel symbols, initializes PTY.
 * Returns 0 on success, -1 on failure.
 */
int kterm_nhook_init(void);

/*
 * Create and show the terminal dialog.
 * Creates the ConfirmationDialog, sets title, creates input field, shows modal.
 * Returns 0 on success, -1 on failure.
 */
int kterm_nhook_dialog_show(void);

#endif /* KTERM_NICKELHOOK_H */
