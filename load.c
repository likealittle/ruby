/*
 * load methods from eval.c
 */

#include "ruby/ruby.h"
#include "ruby/util.h"
#include "dln.h"
#include "eval_intern.h"
#include "assert.h"
VALUE ruby_dln_librefs;

#define IS_RBEXT(e) (strcmp(e, ".rb") == 0)
#define IS_SOEXT(e) (strcmp(e, ".so") == 0 || strcmp(e, ".o") == 0)
#ifdef DLEXT2
#define IS_DLEXT(e) (strcmp(e, DLEXT) == 0 || strcmp(e, DLEXT2) == 0)
#else
#define IS_DLEXT(e) (strcmp(e, DLEXT) == 0)
#endif
#define MAX_FILE_NAME_LENGTH 1000

static const char *const loadable_ext[] = {
    ".rb", DLEXT,
#ifdef DLEXT2
    DLEXT2,
#endif
    0
};

struct file_tree_element{
    char *name;
    int file;
    struct file_tree_element *parent;
    struct file_tree_element *child;
    struct file_tree_element *right_sibling;
};

typedef struct file_tree_element file_tree_node;

file_tree_node *root;

file_tree_node *get_new_node( file_tree_node *pointer){
    pointer = ( file_tree_node *) ( malloc( sizeof(file_tree_node) ) ); 
    pointer -> name = NULL ;
    pointer -> file = 0 ; 
    pointer -> parent = NULL ; 
    pointer -> child = NULL ; 
    pointer -> right_sibling = NULL; 
    return pointer;
}

file_tree_node *get_new_node_str( file_tree_node *pointer, char *s){
    pointer = get_new_node( pointer );
    pointer -> name = ( char * ) ( malloc ( ( strlen(s) + 1 ) * sizeof(char)));
    strcpy( pointer -> name , s );
    return pointer; 
}

file_tree_node *init_root(file_tree_node *root){
    root = get_new_node_str(root,".");
    return root;
}

file_tree_node *get_child(file_tree_node *parent, char *s){
    file_tree_node *child = parent -> child ;
    while ( child != NULL ){
        if ( strcmp( child -> name , s) == 0 )
            return child;
        child = child -> right_sibling;
    }
    return NULL;
}

void print_children(file_tree_node *parent){
    file_tree_node *child = parent -> child ;
    printf("The node %s has the following children:\n" , parent -> name);
    while ( child != NULL ){
        printf("%s ", child -> name);
        child = child -> right_sibling;
    }
    printf("\n");
}
void add_child(file_tree_node *parent, file_tree_node *new_child){
    if ( parent -> child == NULL ){
        parent -> child = new_child ;
    }else{
        file_tree_node *child = parent -> child ;
        while ( child -> right_sibling != NULL )
            child = child -> right_sibling;
        child -> right_sibling = new_child ;
    }
}
static void print_str_ary(VALUE ary)
{
    long n = RARRAY_LEN(ary);
    long i;
    printf("\n{\n");

    for(i =0; i < n; i++)
    {
        printf("%s,\n", RSTRING_PTR( RARRAY_PTR(ary)[i]));
    }
    printf("}\n");
}
// Function to //print a ruby string from "VALUE arr".
static void print_str(VALUE ary){
    //printf("%s\n" , RSTRING_PTR( ary));
}
VALUE
rb_get_load_path(void)
{
    ////printf("Function 2"); 
    VALUE load_path = GET_VM()->load_path;
    ////printf("Load path has these elements:"); 
    ////print_str_ary(load_path);
    return load_path;
}

VALUE
rb_get_expanded_load_path(void)
{
    //printf("Function 3"); 
    VALUE load_path = rb_get_load_path();
    VALUE ary;
    long i;

    ary = rb_ary_new2(RARRAY_LEN(load_path));
    for (i = 0; i < RARRAY_LEN(load_path); ++i) {
	VALUE path = rb_file_expand_path(RARRAY_PTR(load_path)[i], Qnil);
	rb_str_freeze(path);
	rb_ary_push(ary, path);
    }
    rb_obj_freeze(ary);
    return ary;
}

static VALUE
load_path_getter(ID id, rb_vm_t *vm)
{
    //printf("Function 4"); 
    return vm->load_path;
}

static VALUE
get_loaded_features(void)
{
    //printf("Function 5"); 
    return GET_VM()->loaded_features;
}

static st_table *
get_dollar_quote(void)
{
   //printf("Function 6"); 
    return GET_VM()-> dollar_quote;
}
static st_table *
get_loading_table(void)
{
    //printf("Function 6"); 
    return GET_VM()->loading_table;
}

// FIXME: For adding files which are in the default ruby path! Not useful for the problem statement. But needed for the patch. 
static VALUE loaded_feature_path(const char *name, long vlen, const char *feature, long len, int type, VALUE load_path){
    //printf("Function 7"); 
    long i;
    // Loop through every element in $:
    for (i = 0; i < RARRAY_LEN(load_path); ++i) {
        VALUE p = RARRAY_PTR(load_path)[i];
        const char *s = StringValuePtr(p);
        long n = RSTRING_LEN(p);
        if (vlen < n + len + 1) continue;
        if (n && (strncmp(name, s, n) || name[n] != '/')) continue;
        if (strncmp(name + n + 1, feature, len)) continue;
        if (name[n+len+1] && name[n+len+1] != '.') continue;
        switch (type) {
            case 's':
                if (IS_DLEXT(&name[n+len+1])) return p;
                break;
            case 'r':
                if (IS_RBEXT(&name[n+len+1])){
                    return p;
                }
                break;
            default:
                return p;
        }
    }
    return 0;
}

struct loaded_feature_searching {
    const char *name;
    long len;
    int type;
    VALUE load_path;
    const char *result;
};

static int
loaded_feature_path_i(st_data_t v, st_data_t b, st_data_t f)
{
    //printf("Function 8"); 
    const char *s = (const char *)v;
    struct loaded_feature_searching *fp = (struct loaded_feature_searching *)f;
    VALUE p = loaded_feature_path(s, strlen(s), fp->name, fp->len,
				  fp->type, fp->load_path);
    if (!p) return ST_CONTINUE;
    fp->result = s;
    return ST_STOP;
}

int cnt = 0 ; 
static int rb_feature_p(const char *feature, const char *ext, int rb, int expanded, const char **fn)
{
    ////printf("Function 9"); 
    //printf("Feature, ext, rb , exp = %s %s %d %d\n",feature,ext,rb,expanded);
    VALUE v, features, p, load_path = 0;
    const char *f, *e;
    long i, j, len, elen, n;
    st_table *loading_tbl;
    st_data_t data;
    int type;

    int already_found = 0 ; 
    //Initialize return value
    if (fn) *fn = 0;
    // If it has an extension, get the details
    if (ext) {
        elen= strlen(ext);
        len = strlen(feature) - elen;
        type = rb ? 'r' : 's';
    }
    else {
        len = strlen(feature);
        elen = 0;
        type = 0;
    }

    ////printf("Length of the feature we're trying to add is %d\n" , len ) ; 
    // Does this give $" ? I'm not very sure. I'm going ahead with that assumption. 
    features = get_loaded_features();
    //printf("\n\nElements in $:= ") ;
    //print_str_ary(rb_get_expanded_load_path());
    //printf("\n\nElements in $\"= ") ;
    //print_str_ary(features);
    char *which = feature ;
    int found = 0 ; 
    found = st_lookup( get_dollar_quote() , feature , &found ) ; 
    if ( !ext && ! found ){
        for ( i = 0 ; loadable_ext[i] ; i ++ ){
            char t[MAX_FILE_NAME_LENGTH];
            strcpy(t,feature);
            strcat(t,loadable_ext[i]);
            int temp;
            temp = st_lookup( get_dollar_quote() , t , &temp );
            //printf("Checking with %s\n" , t ); 
            if ( temp ){
                found = 1 ;
                expanded = 1 ; 
                which = t ;
                //printf("Worked with %s\n" , t ); 
                break;
            }

        }
    }

    // FIXME:Worry about loaded feature path later! For the second problem statement, that is. 
    if (! expanded  && !found ){
        load_path = rb_get_expanded_load_path();
        // Loop through every element in $:
        for (i = 0; i < RARRAY_LEN(load_path) && !found; ++i) {
            VALUE path = RARRAY_PTR(load_path)[i];
            char *s = StringValuePtr(path);
            strcat(s,"/");
            strcat(s,feature);
            if (ext){
                int temp = 0 ;
                //printf("Checking with %s\n" , s ); 
                temp = st_lookup( get_dollar_quote() , s , &temp );
                if ( temp ){
                    found = 1 ;
                    which = s ;
                    //printf("Worked with %s\n" , s ); 
                    expanded = 1 ; 
                    break;
                }
            }else{
                for ( j = 0 ; loadable_ext[j] ; j++ ){
                    char t[MAX_FILE_NAME_LENGTH];
                    strcpy(t,s);
                    //printf("t = %s\n" , t ) ; 
                    //printf("111. Checking with %s\n" , t ); 
                    strcat(t,loadable_ext[j] );
                    int temp;
                    temp = st_lookup( get_dollar_quote() , t , &temp );
                    //printf("222. Checking with %s\n" , t ); 
                    if ( temp ){
                        found = 1 ;
                        expanded = 1 ; 
                        which = t ;
                        //printf("Worked with %s\n" , t ); 
                        break;
                    }
                }
            }
        }
    }
    char ret = 0 ;
    if ( found  ){
        //printf(" Which = %s Found = %d " , which , found ); 
        e = strrchr( which , '.'); 
        //printf("Feature %s was found in the hash table!\n" , feature);
        //printf("Extension  = %s\n" , e ); 
        if ( !*e ){
            if ( !ext ){
                //printf("Will return U!\n");
                ret = 'u';
                return 'u';
            }
        }
        if ( *e == '.' ){
            if ((!rb || !ext) && (IS_SOEXT(e) || IS_DLEXT(e))) {
                //printf("Will return S!\n");
                ret = 's';
                return 's';
            }
            if ((rb || !ext) && (IS_RBEXT(e))) {
                //printf("Will return R!\n");
                ret ='r';
                return 'r';
            }
        }
    }

   // for (i = 0; i < RARRAY_LEN(features); ++i) {
   //     // Pointer to i'th feature
   //     v = RARRAY_PTR(features)[i];
   //     // String value of the pointer
   //     f = StringValuePtr(v);
   //     //printf("f = %s\n", f);
   //     /* if the expanded path of the feature in $" is less than the absolute/relative path of the feature to be added
   //        then you can skip it */
   //     if ((n = RSTRING_LEN(v)) < len) continue;
   //     if (strncmp(f, feature, len) != 0) {
   //         if (expanded) continue;
   //         if (!load_path) 
   //             load_path = rb_get_expanded_load_path();
   //         p = loaded_feature_path(f, n, feature, len, type, load_path);
   //         if (!p)
   //             continue;
   //         printf("feature = %s and p = %s\n" , feature , StringValuePtr(p) ) ; 
   //         // Not going to come here for the current problem statement. 
   //         expanded = 1;
   //         f += RSTRING_LEN(p) + 1;
   //     }
   //     // All this happens only when the feature is already found in $"
   //    e = f + len ; 
   //    //print_str_ary(get_loaded_features() ); 
   //    printf("feature = %s e = %s f = %s len = %d strlen(feature) = %d elen = %d\n" , feature , e , f , len , strlen(feature) ,elen) ; 
   //     if (!*e) {
   //         if (ext) continue;
   //         printf("prev ret = %c\n" , ret ) ; 
   //         assert( ret == 'u' ); 
   //         return 'u';
   //     }
   //     if (*e != '.') continue;
   //     if ((!rb || !ext) && (IS_SOEXT(e) || IS_DLEXT(e))) {
   //         printf("prev ret = %c\n" , ret ) ; 
   //         assert( ret == 's') ;
   //         return 's';
   //     }
   //     if ((rb || !ext) && (IS_RBEXT(e))) {
   //         printf("Could find the %s feature in the trie!\n" , feature);
   //         printf("prev ret = %c\n" , ret ) ; 
   //         assert( ret == 'r' );
   //         return 'r';
   //     }
   // }
    //printf("\nCouldn't find %s feature in the trie!\n" ,  feature);
    loading_tbl = get_loading_table();
    if (loading_tbl) {
        f = 0;
        if (!expanded) {
            struct loaded_feature_searching fs;
            fs.name = feature;
            fs.len = len;
            fs.type = type;
            fs.load_path = load_path ? load_path : rb_get_load_path();
            fs.result = 0;
            st_foreach(loading_tbl, loaded_feature_path_i, (st_data_t)&fs);
            //printf("fs.result = %s\n" , fs.result);
            if ((f = fs.result) != 0) {
                if (fn) *fn = f;
                //printf("1. Adding %s to the $\n" , feature);
                if (!ext) return 'u';
                return !IS_RBEXT(ext) ? 's' : 'r';
            }
        }
        if (st_get_key(loading_tbl, (st_data_t)feature, &data)) {
            if (fn) *fn = (const char*)data;
            //printf("1. Adding %s to the $\n" , feature);
            if (!ext) return 'u';
            return !IS_RBEXT(ext) ? 's' : 'r';
        }
        else {
            VALUE bufstr;
            char *buf;

            if (ext && *ext){
                //printf("\n 1. Couldn't load the feature %s\n" , feature) ; 
                return 0;
            }
            bufstr = rb_str_tmp_new(len + DLEXT_MAXLEN);
            buf = RSTRING_PTR(bufstr);
            MEMCPY(buf, feature, char, len);
            for (i = 0; (e = loadable_ext[i]) != 0; i++) {
                strlcpy(buf + len, e, DLEXT_MAXLEN + 1);
                if (st_get_key(loading_tbl, (st_data_t)buf, &data)) {
                    rb_str_resize(bufstr, 0);
                    if (fn) *fn = (const char*)data;
                   // printf("2. Adding %s to the $\n" , feature);
                    return i ? 's' : 'r';
                }
            }
            rb_str_resize(bufstr, 0);
        }
    }
    //printf("\n3. Couldn't load the feature %s\n" , feature) ; 
    return 0;
}

int
rb_provided(const char *feature)
{
    //printf("Function 10"); 
    return rb_feature_provided(feature, 0);
}

int
rb_feature_provided(const char *feature, const char **loading)
{
    //printf("Function 11"); 
    const char *ext = strrchr(feature, '.');
    volatile VALUE fullpath = 0;
    ////printf("Feature before = %s\n", feature );
    if (*feature == '.' &&
            (feature[1] == '/' || strncmp(feature+1, "./", 2) == 0)) {
        fullpath = rb_file_expand_path(rb_str_new2(feature), Qnil);
        feature = RSTRING_PTR(fullpath);
        ////printf(" Feature after = %s\n" , feature);
    }
    if (ext && !strchr(ext, '/')) {
        if (IS_RBEXT(ext)) {
            if (rb_feature_p(feature, ext, TRUE, FALSE, loading)) return TRUE;
            return FALSE;
        }
        else if (IS_SOEXT(ext) || IS_DLEXT(ext)) {
            if (rb_feature_p(feature, ext, FALSE, FALSE, loading)) return TRUE;
            return FALSE;
        }
    }
    if (rb_feature_p(feature, 0, TRUE, FALSE, loading))
        return TRUE;
    return FALSE;
}

static void
rb_provide_feature(VALUE feature){
    st_add_direct( get_dollar_quote() , StringValuePtr(feature) , 42 ); 
    rb_ary_push(get_loaded_features(), feature);
}

void
rb_provide(const char *feature)
{
    //printf("Function 13"); 
    rb_provide_feature(rb_usascii_str_new2(feature));
}

NORETURN(static void load_failed(VALUE));

static void
rb_load_internal(VALUE fname, int wrap)
{
    //printf("Function 14"); 
    int state;
    rb_thread_t *th = GET_THREAD();
    volatile VALUE wrapper = th->top_wrapper;
    volatile VALUE self = th->top_self;
    volatile int loaded = FALSE;
    volatile int mild_compile_error;
#ifndef __GNUC__
    rb_thread_t *volatile th0 = th;
#endif

    th->errinfo = Qnil; /* ensure */

    if (!wrap) {
	rb_secure(4);		/* should alter global state */
	th->top_wrapper = 0;
    }
    else {
	/* load in anonymous module as toplevel */
	th->top_self = rb_obj_clone(rb_vm_top_self());
	th->top_wrapper = rb_module_new();
	rb_extend_object(th->top_self, th->top_wrapper);
    }

    mild_compile_error = th->mild_compile_error;
    PUSH_TAG();
    state = EXEC_TAG();
    if (state == 0) {
	NODE *node;
	VALUE iseq;

	th->mild_compile_error++;
	node = (NODE *)rb_load_file(RSTRING_PTR(fname));
	loaded = TRUE;
	iseq = rb_iseq_new_top(node, rb_str_new2("<top (required)>"), fname, fname, Qfalse);
	th->mild_compile_error--;
	rb_iseq_eval(iseq);
    }
    POP_TAG();

#ifndef __GNUC__
    th = th0;
    fname = RB_GC_GUARD(fname);
#endif
    th->mild_compile_error = mild_compile_error;
    th->top_self = self;
    th->top_wrapper = wrapper;

    if (!loaded) {
	rb_exc_raise(GET_THREAD()->errinfo);
    }
    if (state) {
	rb_vm_jump_tag_but_local_jump(state, Qundef);
    }

    if (!NIL_P(GET_THREAD()->errinfo)) {
	/* exception during load */
	rb_exc_raise(th->errinfo);
    }
}

void
rb_load(VALUE fname, int wrap)
{
    //printf("Function 15"); 
    VALUE tmp = rb_find_file(FilePathValue(fname));
    if (!tmp) load_failed(fname);
    rb_load_internal(tmp, wrap);
}

void
rb_load_protect(VALUE fname, int wrap, int *state)
{
    //printf("Function 16"); 
    int status;

    PUSH_TAG();
    if ((status = EXEC_TAG()) == 0) {
	rb_load(fname, wrap);
    }
    POP_TAG();
    if (state)
	*state = status;
}

/*
 *  call-seq:
 *     load(filename, wrap=false)   -> true
 *
 *  Loads and executes the Ruby
 *  program in the file _filename_. If the filename does not
 *  resolve to an absolute path, the file is searched for in the library
 *  directories listed in <code>$:</code>. If the optional _wrap_
 *  parameter is +true+, the loaded script will be executed
 *  under an anonymous module, protecting the calling program's global
 *  namespace. In no circumstance will any local variables in the loaded
 *  file be propagated to the loading environment.
 */

static VALUE
rb_f_load(int argc, VALUE *argv)
{
    //printf("Function 17"); 
    VALUE fname, wrap, path;

    rb_scan_args(argc, argv, "11", &fname, &wrap);
    path = rb_find_file(FilePathValue(fname));
    if (!path) {
	if (!rb_file_load_ok(RSTRING_PTR(fname)))
	    load_failed(fname);
	path = fname;
    }
    rb_load_internal(path, RTEST(wrap));
    return Qtrue;
}

static char *
load_lock(const char *ftptr)
{
    //printf("Function 18"); 
    st_data_t data;
    st_table *loading_tbl = get_loading_table();

    if (!loading_tbl || !st_lookup(loading_tbl, (st_data_t)ftptr, &data)) {
	/* loading ruby library should be serialized. */
	if (!loading_tbl) {
	    GET_VM()->loading_table = loading_tbl = st_init_strtable();
	}
	/* partial state */
	ftptr = ruby_strdup(ftptr);
	data = (st_data_t)rb_barrier_new();
	st_insert(loading_tbl, (st_data_t)ftptr, data);
	return (char *)ftptr;
    }
    if (RTEST(ruby_verbose)) {
	rb_warning("loading in progress, circular require considered harmful - %s", ftptr);
	rb_backtrace();
    }
    return RTEST(rb_barrier_wait((VALUE)data)) ? (char *)ftptr : 0;
}

static void
load_unlock(const char *ftptr, int done)
{
    //printf("Function 19"); 
    if (ftptr) {
	st_data_t key = (st_data_t)ftptr;
	st_data_t data;
	st_table *loading_tbl = get_loading_table();

	if (st_delete(loading_tbl, &key, &data)) {
	    VALUE barrier = (VALUE)data;
	    xfree((char *)key);
	    if (done)
		rb_barrier_destroy(barrier);
	    else
		rb_barrier_release(barrier);
	}
    }
}


/*
 *  call-seq:
 *     require(string)    -> true or false
 *
 *  Ruby tries to load the library named _string_, returning
 *  +true+ if successful. If the filename does not resolve to
 *  an absolute path, it will be searched for in the directories listed
 *  in <code>$:</code>. If the file has the extension ``.rb'', it is
 *  loaded as a source file; if the extension is ``.so'', ``.o'', or
 *  ``.dll'', or whatever the default shared library extension is on
 *  the current platform, Ruby loads the shared library as a Ruby
 *  extension. Otherwise, Ruby tries adding ``.rb'', ``.so'', and so on
 *  to the name. The name of the loaded feature is added to the array in
 *  <code>$"</code>. A feature will not be loaded if its name already
 *  appears in <code>$"</code>. The file name is converted to an absolute
 *  path, so ``<code>require 'a'; require './a'</code>'' will not load
 *  <code>a.rb</code> twice.
 *
 *     require "my-library.rb"
 *     require "db-driver"
 */

VALUE
rb_f_require(VALUE obj, VALUE fname)
{
    //printf("Function 20\n"); 
    //printf("\n\nRB_F_Require! Elements in $\"= ") ;
    //print_str_ary(get_loaded_features());
    return rb_require_safe(fname, rb_safe_level());
}

VALUE
rb_f_require_relative(VALUE obj, VALUE fname)
{
    //printf("Function 21\n"); 
    VALUE rb_current_realfilepath(void);
    VALUE base = rb_current_realfilepath();
    if (NIL_P(base)) {
	rb_raise(rb_eLoadError, "cannot infer basepath");
    }
    base = rb_file_dirname(base);
    return rb_require_safe(rb_file_absolute_path(fname, base), rb_safe_level());
}

static int search_required(VALUE fname, volatile VALUE *path, int safe_level)
{
    //printf("\n\nSearch Required! Elements in $\"= ") ;
    //print_str_ary(get_loaded_features());
    //printf("\n$:=");
    //print_str_ary(rb_get_load_path() );
    //printf("Function 22"); 
    //printf("Searching required...\n");
    VALUE tmp;
    char *ext, *ftptr;
    int type, ft = 0;
    const char *loading;

    *path = 0;
    //printf("The file name is : ") ;
    //print_str(fname);
    ext = strrchr(ftptr = RSTRING_PTR(fname), '.');
    //printf(" Extension = %s\n" , ext ) ; 
    // This is a file with a proper extension.
    if (ext && !strchr(ext, '/')) {
        // If it is a ruby file
        if (IS_RBEXT(ext)) {
            //printf("\nFPTR = %s \n" , ftptr );
            // Check if the current path is found
            if (rb_feature_p(ftptr, ext, TRUE, FALSE, &loading)) {
                if (loading) 
                    *path = rb_str_new2(loading);
                //printf("path1= %s\n", path ) ;
                return 'r';
            }
            //printf("Middle!\n");
            // If the current path is not found, try to find if the absolute path is present. 
            if ((tmp = rb_find_file_safe(fname, safe_level)) != 0) {
                ext = strrchr(ftptr = RSTRING_PTR(tmp), '.');
                //printf("\nNew FTPTR = %s\n" , ftptr);
                if (!rb_feature_p(ftptr, ext, TRUE, TRUE, &loading) || loading)
                    *path = tmp;
                return 'r';
            }
            //printf("Returning zero then!\n");
            //printf("\n\n1. After Addition");
            //print_str_ary( get_loaded_features() ); 
            return 0;
        }
        // Don't worry about this part now! This part deals with shared objects etc which are not part of the problem statement!
        else if (IS_SOEXT(ext)) {
            if (rb_feature_p(ftptr, ext, FALSE, FALSE, &loading)) {
                if (loading) *path = rb_str_new2(loading);
                return 's';
            }
            tmp = rb_str_new(RSTRING_PTR(fname), ext - RSTRING_PTR(fname));
#ifdef DLEXT2
            OBJ_FREEZE(tmp);
            if (rb_find_file_ext_safe(&tmp, loadable_ext + 1, safe_level)) {
                ext = strrchr(ftptr = RSTRING_PTR(tmp), '.');
                if (!rb_feature_p(ftptr, ext, FALSE, TRUE, &loading) || loading)
                    *path = tmp;
                return 's';
            }
#else
            rb_str_cat2(tmp, DLEXT);
            OBJ_FREEZE(tmp);
            if ((tmp = rb_find_file_safe(tmp, safe_level)) != 0) {
                ext = strrchr(ftptr = RSTRING_PTR(tmp), '.');
                if (!rb_feature_p(ftptr, ext, FALSE, TRUE, &loading) || loading)
                    *path = tmp;
                return 's';
            }
#endif
        }
        else if (IS_DLEXT(ext)) {
            //printf("There!");
            if (rb_feature_p(ftptr, ext, FALSE, FALSE, &loading)) {
                if (loading) *path = rb_str_new2(loading);
                return 's';
            }
            if ((tmp = rb_find_file_safe(fname, safe_level)) != 0) {
                ext = strrchr(ftptr = RSTRING_PTR(tmp), '.');
                if (!rb_feature_p(ftptr, ext, FALSE, TRUE, &loading) || loading)
                    *path = tmp;
                return 's';
            }
        }
    }
    // This is a file without proper extension.
    // TODO: Later, please!
    else if ((ft = rb_feature_p(ftptr, 0, FALSE, FALSE, &loading)) == 'r') {
        if (loading) *path = rb_str_new2(loading);
        return 'r';
    }

    //FIXME: Don't worry about these either. We're trying to work only with ruby files.
    //print_str_ary( get_loaded_features() ); 
    tmp = fname;
    type = rb_find_file_ext_safe(&tmp, loadable_ext, safe_level);
    switch (type) {
        case 0:
            if (ft)
                break;
            ftptr = RSTRING_PTR(tmp);
            //printf("\nAfter1");
            //print_str_ary( get_loaded_features() ); 
            return rb_feature_p(ftptr, 0, FALSE, TRUE, 0);

        default:
            if (ft)
                break;
        case 1:
            ext = strrchr(ftptr = RSTRING_PTR(tmp), '.');
            if (rb_feature_p(ftptr, ext, !--type, TRUE, &loading) && !loading)
                break;
            *path = tmp;
    }
    //printf("\n\n2. After addition");
    //print_str_ary( get_loaded_features() ); 
    return type ? 's' : 'r';
}

static void
load_failed(VALUE fname)
{
    //printf("Function 23"); 
    VALUE mesg = rb_str_buf_new_cstr("no such file to load -- ");
    rb_str_append(mesg, fname);	/* should be ASCII compatible */
    rb_exc_raise(rb_exc_new3(rb_eLoadError, mesg));
}

static VALUE
load_ext(VALUE path)
{
    //printf("Function 24"); 
    SCOPE_SET(NOEX_PUBLIC);
    return (VALUE)dln_load(RSTRING_PTR(path));
}

VALUE
rb_require_safe(VALUE fname, int safe)
{
    //printf("Function 25"); 
    //printf("\n\nRB_Require_Safe! Elements in $\"= ") ;
    //print_str_ary(get_loaded_features());
    volatile VALUE result = Qnil;
    rb_thread_t *th = GET_THREAD();
    volatile VALUE errinfo = th->errinfo;
    int state;
    struct {
        int safe;
    } volatile saved;
    char *volatile ftptr = 0;

    PUSH_TAG();
    saved.safe = rb_safe_level();
    if ((state = EXEC_TAG()) == 0) {
        VALUE path;
        long handle;
        int found;

        rb_set_safe_level_force(safe);
        FilePathValue(fname);
        rb_set_safe_level_force(0);
        found = search_required(fname, &path, safe);
        //printf("Found = %d\n" , found) ; 
        //printf("\nBefore:");
        //print_str_ary(get_loaded_features() );
        //printf("Found = %c\n" , found ) ; 
        if (found) {
            //if ( path )
                //printf("\nPath returned by search_required = %s\n" , RSTRING_PTR(path)); 
            if (!path || !(ftptr = load_lock(RSTRING_PTR(path)))) {
                //printf("Come 1\n");
                result = Qfalse;
            }
            else {
                //printf("Come 2\n");
                switch (found) {
                    case 'r':
                        rb_load_internal(path, 0);
                        break;

                    case 's':
                        handle = (long)rb_vm_call_cfunc(rb_vm_top_self(), load_ext,
                                path, 0, path, path);
                        rb_ary_push(ruby_dln_librefs, LONG2NUM(handle));
                        break;
                }
                // Add to our hash table!
                //printf("Adding %s to the hash table!\n" , StringValuePtr(path) );
                rb_provide_feature(path);
                result = Qtrue;
            }
            //printf("\nAfter:");
            //print_str_ary(get_loaded_features() );
        }
    }
    POP_TAG();
    load_unlock(ftptr, !state);

    rb_set_safe_level_force(saved.safe);
    if (state) {
        JUMP_TAG(state);
    }

    if (NIL_P(result)) {
        load_failed(fname);
    }

    th->errinfo = errinfo;

    return result;
}

VALUE
rb_require(const char *fname)
{
    VALUE fn = rb_str_new2(fname);
    OBJ_FREEZE(fn);
    return rb_require_safe(fn, rb_safe_level());
}

static VALUE
init_ext_call(VALUE arg)
{
    //printf("Function 27"); 
    SCOPE_SET(NOEX_PUBLIC);
    (*(void (*)(void))arg)();
    return Qnil;
}

void
ruby_init_ext(const char *name, void (*init)(void))
{
    //printf("Function 28"); 
    if (load_lock(name)) {
	rb_vm_call_cfunc(rb_vm_top_self(), init_ext_call, (VALUE)init,
			 0, rb_str_new2(name), Qnil);
	rb_provide(name);
	load_unlock(name, 1);
    }
}

/*
 *  call-seq:
 *     mod.autoload(module, filename)   -> nil
 *
 *  Registers _filename_ to be loaded (using <code>Kernel::require</code>)
 *  the first time that _module_ (which may be a <code>String</code> or
 *  a symbol) is accessed in the namespace of _mod_.
 *
 *     module A
 *     end
 *     A.autoload(:B, "b")
 *     A::B.doit            # autoloads "b"
 */

static VALUE
rb_mod_autoload(VALUE mod, VALUE sym, VALUE file)
{
    //printf("Function 29"); 
    ID id = rb_to_id(sym);

    FilePathValue(file);
    rb_autoload(mod, id, RSTRING_PTR(file));
    return Qnil;
}

/*
 *  call-seq:
 *     mod.autoload?(name)   -> String or nil
 *
 *  Returns _filename_ to be loaded if _name_ is registered as
 *  +autoload+ in the namespace of _mod_.
 *
 *     module A
 *     end
 *     A.autoload(:B, "b")
 *     A.autoload?(:B)            #=> "b"
 */

static VALUE
rb_mod_autoload_p(VALUE mod, VALUE sym)
{
    //printf("Function 30"); 
    return rb_autoload_p(mod, rb_to_id(sym));
}

/*
 *  call-seq:
 *     autoload(module, filename)   -> nil
 *
 *  Registers _filename_ to be loaded (using <code>Kernel::require</code>)
 *  the first time that _module_ (which may be a <code>String</code> or
 *  a symbol) is accessed.
 *
 *     autoload(:MyModule, "/usr/local/lib/modules/my_module.rb")
 */

static VALUE
rb_f_autoload(VALUE obj, VALUE sym, VALUE file)
{
    //printf("Function 31"); 
    VALUE klass = rb_vm_cbase();
    if (NIL_P(klass)) {
	rb_raise(rb_eTypeError, "Can not set autoload on singleton class");
    }
    return rb_mod_autoload(klass, sym, file);
}

/*
 *  call-seq:
 *     autoload?(name)   -> String or nil
 *
 *  Returns _filename_ to be loaded if _name_ is registered as
 *  +autoload+.
 *
 *     autoload(:B, "b")
 *     autoload?(:B)            #=> "b"
 */

static VALUE
rb_f_autoload_p(VALUE obj, VALUE sym)
{
    //printf("Function 32"); 
    /* use rb_vm_cbase() as same as rb_f_autoload. */
    VALUE klass = rb_vm_cbase();
    if (NIL_P(klass)) {
	return Qnil;
    }
    return rb_mod_autoload_p(klass, sym);
}


/* This gets triggered everytime a $: << operation happens */
VALUE append_load_path(VALUE ary, VALUE path)
{
    print_str_ary(ary);
    printf("Adding the path : %s\n" , StringValuePtr(path)); 
    VALUE ret = rb_ary_push(ary, path);
    print_str_ary(ary);
    return ret;
}
void
Init_load()
{
    //printf("Function 33"); 
#undef rb_intern
#define rb_intern(str) rb_intern2(str, strlen(str))
    rb_vm_t *vm = GET_VM();
    //printf("I happen, ok?");

    static const char var_load_path[] = "$:";
    ID id_load_path = rb_intern2(var_load_path, sizeof(var_load_path)-1);

    rb_define_hooked_variable(var_load_path, (VALUE*)vm, load_path_getter, rb_gvar_readonly_setter);
    rb_alias_variable(rb_intern("$-I"), id_load_path);
    rb_alias_variable(rb_intern("$LOAD_PATH"), id_load_path);
    vm->load_path = rb_ary_new();

    rb_define_virtual_variable("$\"", get_loaded_features, 0);
    rb_define_virtual_variable("$LOADED_FEATURES", get_loaded_features, 0);
    rb_define_singleton_method(vm->load_path,"<<",append_load_path,1);
    vm->loaded_features = rb_ary_new();

    //printf("Loaded variables = " ) ; 
    //print_str_ary( vm -> loaded_features );


    vm -> dollar_quote = st_init_strtable();

    //st_add_direct( get_dollar_quote() , "enumerator.so" , 42 );

    rb_define_global_function("load", rb_f_load, -1);
    rb_define_global_function("require", rb_f_require, 1);
    rb_define_global_function("require_relative", rb_f_require_relative, 1);
    rb_define_method(rb_cModule, "autoload", rb_mod_autoload, 2);
    rb_define_method(rb_cModule, "autoload?", rb_mod_autoload_p, 1);
    rb_define_global_function("autoload", rb_f_autoload, 2);
    rb_define_global_function("autoload?", rb_f_autoload_p, 1);

    ruby_dln_librefs = rb_ary_new();
    rb_gc_register_mark_object(ruby_dln_librefs);

    //printf("\n\nInit! Elements in $\"= ") ;
    //print_str_ary(get_loaded_features());
}
