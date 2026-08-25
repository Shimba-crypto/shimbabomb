#ifndef SB_VALUE_H
#define SB_VALUE_H

typedef struct AstNode AstNode;

typedef enum {
    VAL_NUMBER, VAL_STRING, VAL_BOOL, VAL_NIL,
    VAL_FUNCTION, VAL_NATIVE,
    VAL_ARRAY, VAL_MAP, VAL_CLASS, VAL_INSTANCE, VAL_ERROR
} ValueType;

typedef struct Value Value;
typedef struct Environment Environment;

typedef Value (*NativeFn)(int arg_count, Value *args);

typedef struct {
    Value *items;
    int count;
    int capacity;
} ValueArray;

typedef struct {
    char *key;
    Value *value;
} MapEntry;

typedef struct {
    char *name;
    char **params;
    int param_count;
    AstNode *body;
    Environment *closure;
} Method;

typedef struct {
    char **fields;
    int field_count;
    Method *methods;
    int method_count;
} ClassDef;

struct Value {
    ValueType type;
    union {
        double number;
        char *string;
        int boolean;

        struct {
            char **params;
            int param_count;
            AstNode *body;
            Environment *closure;
        } function;

        struct {
            NativeFn fn;
            char *name;
        } native;

        ValueArray array;

        struct {
            MapEntry *entries;
            int count;
            int capacity;
        } map;

        ClassDef *class_def;

        struct {
            ClassDef *class_def;
            Value *fields;
            int field_count;
        } instance;

        struct {
            char *message;
        } error;
    } as;
};

Value val_number(double n);
Value val_string(const char *s);
Value val_bool(int b);
Value val_nil(void);
Value val_function(char **params, int param_count, AstNode *body, Environment *closure);
Value val_native(NativeFn fn, const char *name);
Value val_array(void);
Value val_map(void);
Value val_instance(ClassDef *class_def);
Value val_class(ClassDef *class_def);
Value val_error(const char *message);
Value val_map_get(Value *m, const char *key);
void val_map_set(Value *m, const char *key, Value v);
int val_map_has(Value *m, const char *key);
Value val_map_remove(Value *m, const char *key);
void val_free(Value *v);
int val_is_truthy(Value *v);
const char *val_type_name(Value *v);
void val_print(Value *v);
Value val_copy(Value v);
void val_array_push(ValueArray *arr, Value v);
Value val_array_get(ValueArray *arr, int index);
Value val_array_remove(ValueArray *arr, int index);

#endif
