/*
 * load methods from eval.c
 */

#include "ruby/ruby.h"
#include "ruby/util.h"
#include "dln.h"
#include "eval_intern.h"
#include "dir.h"

VALUE ruby_dln_librefs;

#define IS_RBEXT(e) (strcmp(e, ".rb") == 0)
#define IS_SOEXT(e) (strcmp(e, ".so") == 0 || strcmp(e, ".o") == 0)
#ifdef DLEXT2
#define IS_DLEXT(e) (strcmp(e, DLEXT) == 0 || strcmp(e, DLEXT2) == 0)
#else
#define IS_DLEXT(e) (strcmp(e, DLEXT) == 0)
#endif

#define do_hash(key,table) (st_index_t)(*(table)->type->hash)((key))


static const char *const loadable_ext[] = {
  ".rb", DLEXT,
  #ifdef DLEXT2
  DLEXT2,
  #endif
  0
};

typedef struct pri_path {
  int pri;
  VALUE full_path;
} pri_path;


// TODO : Refactor this hash code - better to use in build strhash
/*
 * 32 bit FNV-1 and FNV-1a non-zero initial basis
 *
 * The FNV-1 initial basis is the FNV-0 hash of the following 32 octets:
 *
 *              chongo <Landon Curt Noll> /\../\
 *
 * NOTE: The \'s above are not back-slashing escape characters.
 * They are literal ASCII  backslash 0x5c characters.
 *
 * NOTE: The FNV-1a initial basis is the same value as FNV-1 by definition.
 */
#define FNV1_32A_INIT 0x811c9dc5

/*
 * 32 bit magic FNV-1a prime
 */
#define FNV_32_PRIME 0x01000193

static unsigned long
mystrhash(VALUE arg)
{
  register const char *string = RSTRING_PTR(arg);
  
  register st_index_t hval = FNV1_32A_INIT;
  
  /*
   * FNV-1a hash each octet in the buffer
   */
  while (*string) 
  {
    /* xor the bottom with the current octet */
    hval ^= (unsigned int)*string++;
    
    /* multiply by the 32 bit FNV magic prime mod 2^32 */
    hval *= FNV_32_PRIME;
  }
  return hval;
}
static int
mystrcmp(VALUE str1, VALUE str2)
{
  char *s1 = RSTRING_PTR(str1), *s2 = RSTRING_PTR(str2);
  return strcmp(s1, s2);
}

static const struct st_hash_type type_strmyhash = {
  mystrcmp,
  mystrhash,
};


VALUE
rb_get_load_path(void)
{
  VALUE load_path = GET_VM()->load_path;
  return load_path;
}

VALUE
rb_get_expanded_load_path(void)
{
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

static void
print_str_ary(char * name, VALUE ary)
{
  long n = RARRAY_LEN(ary);
  long i;
  printf("(%s){ ", name);
  
  for(i =0; i < n; i++)
  {
    printf("%s,", RSTRING_PTR( RARRAY_PTR(ary)[i]));
  }
  printf("}\n");
}

static VALUE
load_path_getter(ID id, rb_vm_t *vm)
{
  return vm->load_path;
}

static st_table *
get_loaded_features_hash(void)
{
  struct st_table * ret;
  ret = GET_VM()->loaded_features_hash;
  if(!ret)
    ret = GET_VM()->loaded_features_hash = st_init_strcasetable();
  return ret;
}

static st_table *
get_load_path_files_cache(void)
{
  struct st_table * ret;
  ret = GET_VM()->load_path_files_cache;
  return ret;
}

static int
push_key(st_data_t key, st_data_t val, st_data_t ary)
{
  if(RTEST(val))
  {
    rb_ary_push((VALUE)ary, (VALUE)key);
  }
  return ST_CONTINUE;
}



static VALUE
get_loaded_features(void)
{
  struct st_table * st;
  st = get_loaded_features_hash();
  long n = st->num_entries;
  VALUE ary = rb_ary_new();
  
  st_foreach(st, push_key, ary);
  return ary;
}

static void 
set_loaded_features(VALUE val, ID id, VALUE *var, struct global_entry * entry)
{
  printf("Inside feature setter!!\n");
  struct st_table *st;
  st = get_loaded_features_hash();
  st_insert(st, val, Qtrue);
}

static st_table *
get_loading_table(void)
{
  return GET_VM()->loading_table;
}

static VALUE
loaded_feature_path(const char *name, long vlen, const char *feature, long len,
		    int type, VALUE load_path)
{
  long i;
  long plen;
  const char *e;
  
  if(vlen < len) return 0;
  if (!strncmp(name+(vlen-len),feature,len)){
    plen = vlen - len - 1;
  } else {
    for (e = name + vlen; name != e && *e != '.' && *e != '/'; --e);
    if (*e!='.' ||
      e-name < len ||
      strncmp(e-len,feature,len) )
      return 0;
    plen = e - name - len - 1;
  }
  for (i = 0; i < RARRAY_LEN(load_path); ++i) {
    VALUE p = RARRAY_PTR(load_path)[i];
    const char *s = StringValuePtr(p);
    long n = RSTRING_LEN(p);
    
    if (n != plen ) continue;
    if (n && (strncmp(name, s, n) || name[n] != '/')) continue;
    switch (type) {
      case 's':
	if (IS_DLEXT(&name[n+len+1])) return p;
	break;
      case 'r':
	if (IS_RBEXT(&name[n+len+1])) return p;
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
  const char *s = (const char *)v;
  struct loaded_feature_searching *fp = (struct loaded_feature_searching *)f;
  VALUE p = loaded_feature_path(s, strlen(s), fp->name, fp->len,
				fp->type, fp->load_path);
  if (!p) return ST_CONTINUE;
  fp->result = s;
  return ST_STOP;
}

static int
rb_my_feature2_p(const char *feature, int len, const char *ext,
    struct st_table *st, VALUE load_path)
{
  //FIXME
  //load_path is basically ignored here
  VALUE file, query;
  long i;
  if(ext)
    file = rb_str_append(rb_str_new(feature, len), rb_str_new(ext, strlen(ext)));
  else
    file = rb_str_new(feature, len);

  if(st_lookup(st, file, 0))
    return 1;

  char *first_elem;
  int first_elem_len = strchrnul(RSTRING_PTR(file), '/') - RSTRING_PTR(file);
  first_elem = malloc(first_elem_len + 1);
  memcpy(first_elem, RSTRING_PTR(file), first_elem_len);
  *(first_elem + first_elem_len) = 0;

  VALUE *paths_arr = malloc(sizeof(VALUE));
  struct st_table *lpfc = get_load_path_files_cache();

  if (st_lookup(lpfc, rb_str_new2(first_elem), paths_arr)) {
    //TODO sort paths_arr acc to priority
    //if only appends are done to $:, this array is already sorted
    for (i = 0; i < RARRAY_LEN(*paths_arr); ++i) 
    {
      pri_path *p = RARRAY_PTR(*paths_arr)[i];
      query = rb_file_expand_path(file, p->full_path);
      if(st_lookup(st, query, 0)) 
      {
        return 1;
      }
    }
    return 0;
  }
  else {
    //FIXME
    //Should never come here if our lpfc and $: are in sync
    //But if it does, do a linear trav of load_path
    return 0;
    for (i = 0; i < RARRAY_LEN(load_path); ++i) 
    {
      VALUE p = RARRAY_PTR(load_path)[i];    
      query = rb_file_expand_path(file, p);
      if(st_lookup(st, query, 0)) 
        return 1;
    }
    return 0;
  }
  return 0;
}


static int
rb_feature_p(const char *feature, const char *ext, int rb, int expanded, const char **fn)
{
  VALUE v, p, load_path = 0;
  const char *f, *e;
  long i, len, elen, n;
  st_table *loading_tbl;
  st_data_t data;
  int type;
  struct st_table * features;
  features = get_loaded_features_hash();
  load_path = rb_load_path();
  
  if (fn) *fn = 0;
  if (ext) 
  {
    elen = strlen(ext);
    len = strlen(feature) - elen;
    type = rb ? 'r' : 's';
    if(rb_my_feature2_p(feature, len, ext, features, load_path)) 
      return type;
  }
  else 
  {
    len = strlen(feature);
    elen = 0;
    if(rb_my_feature2_p(feature, len, 0, features, load_path)) return 'u';
    if(rb_my_feature2_p(feature, len, ".so", features, load_path)) return 's';
    if(rb_my_feature2_p(feature, len, ".rb", features, load_path)) return 'r';
  }
  loading_tbl = get_loading_table();
  if (loading_tbl) 
  {
    f = 0;
    if (!expanded) 
    {
      struct loaded_feature_searching fs;
      fs.name = feature;
      fs.len = len;
      fs.type = type;
      fs.load_path = load_path ? load_path : rb_get_load_path();
      fs.result = 0;
      st_foreach(loading_tbl, loaded_feature_path_i, (st_data_t)&fs);
      if ((f = fs.result) != 0) {
	if (fn) *fn = f;
	goto loading;
      }
    }
    if (st_get_key(loading_tbl, (st_data_t)feature, &data)) 
    {
      if (fn) *fn = (const char*)data;
      loading:
      if (!ext) return 'u';
      return !IS_RBEXT(ext) ? 's' : 'r';
    }
    else 
    {
      VALUE bufstr;
      char *buf;
      
      if (ext && *ext) return 0;
      bufstr = rb_str_tmp_new(len + DLEXT_MAXLEN);
      buf = RSTRING_PTR(bufstr);
      MEMCPY(buf, feature, char, len);
      for (i = 0; (e = loadable_ext[i]) != 0; i++) {
	strlcpy(buf + len, e, DLEXT_MAXLEN + 1);
	if (st_get_key(loading_tbl, (st_data_t)buf, &data)) {
	  rb_str_resize(bufstr, 0);
	  if (fn) *fn = (const char*)data;
	  return i ? 's' : 'r';
	}
      }
      rb_str_resize(bufstr, 0);
    }
  }
  return 0;
}

int
rb_provided(const char *feature)
{
  return rb_feature_provided(feature, 0);
}

int
rb_feature_provided(const char *feature, const char **loading)
{
  const char *ext = strrchr(feature, '.');
  volatile VALUE fullpath = 0;
  
  if (*feature == '.' &&
    (feature[1] == '/' || strncmp(feature+1, "./", 2) == 0)) {
    fullpath = rb_file_expand_path(rb_str_new2(feature), Qnil);
  feature = RSTRING_PTR(fullpath);
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
rb_provide_feature(VALUE feature)
{
  rb_ary_push(GET_VM()->loaded_features, feature);
  struct st_table * st = get_loaded_features_hash();
  int code = st_insert(st, feature, Qtrue);
  
}

void
rb_provide(const char *feature)
{
  rb_provide_feature(rb_usascii_str_new2(feature));
}

NORETURN(static void load_failed(VALUE));

static void
rb_load_internal(VALUE fname, int wrap)
{
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
    rb_secure(4);       /* should alter global state */
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
  VALUE tmp = rb_find_file(FilePathValue(fname));
  if (!tmp) load_failed(fname);
  rb_load_internal(tmp, wrap);
}

void
rb_load_protect(VALUE fname, int wrap, int *state)
{
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
  return rb_require_safe(fname, rb_safe_level());
}

VALUE
rb_f_require_relative(VALUE obj, VALUE fname)
{
  VALUE rb_current_realfilepath(void);
  VALUE base = rb_current_realfilepath();
  if (NIL_P(base)) {
    rb_raise(rb_eLoadError, "cannot infer basepath");
  }
  base = rb_file_dirname(base);
  return rb_require_safe(rb_file_absolute_path(fname, base), rb_safe_level());
}

static int
search_required(VALUE fname, volatile VALUE *path, int safe_level)
{
  VALUE tmp;
  char *ext, *ftptr;
  int type, ft = 0;
  const char *loading;  
  
  *path = 0;
  ext = strrchr(ftptr = RSTRING_PTR(fname), '.');
  if (ext && !strchr(ext, '/')) 
  {
    if (IS_RBEXT(ext)) 
    {
      if (rb_feature_p(ftptr, ext, TRUE, FALSE, &loading)) 
      {
	if (loading) *path = rb_str_new2(loading);
	  return 'r';
      }
      if ((tmp = rb_find_file_safe(fname, safe_level)) != 0) {
	ext = strrchr(ftptr = RSTRING_PTR(tmp), '.');
	if (!rb_feature_p(ftptr, ext, TRUE, TRUE, &loading) || loading)
	  *path = tmp;
	return 'r';
      }
      return 0;
    }
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
  else if ((ft = rb_feature_p(ftptr, 0, FALSE, FALSE, &loading)) == 'r') {
    if (loading) *path = rb_str_new2(loading);
	   return 'r';
  }
  tmp = fname;
  type = rb_find_file_ext_safe(&tmp, loadable_ext, safe_level);
  switch (type) {
    case 0:
      if (ft)
	break;
      ftptr = RSTRING_PTR(tmp);
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
  return type ? 's' : 'r';
}

static void
load_failed(VALUE fname)
{
  VALUE mesg = rb_str_buf_new_cstr("no such file to load -- ");
  rb_str_append(mesg, fname); /* should be ASCII compatible */
  rb_exc_raise(rb_exc_new3(rb_eLoadError, mesg));
}

static VALUE
load_ext(VALUE path)
{
  SCOPE_SET(NOEX_PUBLIC);
  return (VALUE)dln_load(RSTRING_PTR(path));
}

VALUE
rb_require_safe(VALUE fname, int safe)
{
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
    if (found) {
      if (!path || !(ftptr = load_lock(RSTRING_PTR(path)))) {
	result = Qfalse;
      }
      else {
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
	rb_provide_feature(path);
	result = Qtrue;
      }
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
  SCOPE_SET(NOEX_PUBLIC);
  (*(void (*)(void))arg)();
  return Qnil;
}

void
ruby_init_ext(const char *name, void (*init)(void))
{
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
  /* use rb_vm_cbase() as same as rb_f_autoload. */
  VALUE klass = rb_vm_cbase();
  if (NIL_P(klass)) {
    return Qnil;
  }
  return rb_mod_autoload_p(klass, sym);
}

VALUE load_path_append(VALUE ary, VALUE item) {
  //sync the hash with this ary
  VALUE dir_ents = dir_entries(1, &item, rb_cDir);
  struct st_table *lpfc = get_load_path_files_cache();

  while(rb_ary_empty_p(dir_ents) != Qtrue) {
    VALUE ent1 = rb_ary_pop(dir_ents);
    if (rb_str_equal(ent1, rb_str_new2(".")) == Qtrue)
      continue;
    if (rb_str_equal(ent1, rb_str_new2("..")) == Qtrue)
      continue;

    VALUE *paths_arr = malloc(sizeof(VALUE));
    pri_path *cur = malloc(sizeof(pri_path));	
    cur->full_path = rb_file_expand_path(item, Qnil); 

    if (st_lookup(lpfc, ent1, paths_arr)) {
      cur->pri = NUM2INT(rb_ary_length(*paths_arr));
      rb_ary_push(*paths_arr, cur);
    }
    else {
      *paths_arr = rb_ary_new();
      cur->pri = 0;
      rb_ary_push(*paths_arr, cur);
      st_insert(lpfc, ent1, *paths_arr);
    }
    /*rb_ary_push(ary, ent1);*/
    /*rb_ary_push(ary, INT2NUM(cur->pri));*/
    /*rb_ary_push(ary, cur->full_path);*/
  }
  return rb_ary_push(ary, item);
}

void
Init_load()
{
  #undef rb_intern
  #define rb_intern(str) rb_intern2(str, strlen(str))
    rb_vm_t *vm = GET_VM();
    static const char var_load_path[] = "$:";
    ID id_load_path = rb_intern2(var_load_path, sizeof(var_load_path)-1);
    
    rb_define_hooked_variable(var_load_path, (VALUE*)vm, load_path_getter, rb_gvar_readonly_setter);
    rb_alias_variable(rb_intern("$-I"), id_load_path);
    rb_alias_variable(rb_intern("$LOAD_PATH"), id_load_path);
    vm->load_path = rb_ary_new();
    
    rb_define_singleton_method(vm->load_path, "<<", load_path_append, 1);

    rb_define_virtual_variable("$\"", get_loaded_features, 0);
    rb_define_virtual_variable("$LOADED_FEATURES", get_loaded_features, 0);
    vm->loaded_features = rb_ary_new();
    vm->loaded_features_hash = st_init_table(&type_strmyhash);
    vm->load_path_files_cache = st_init_table(&type_strmyhash);
    
    rb_define_global_function("load", rb_f_load, -1);
    rb_define_global_function("require", rb_f_require, 1);
    rb_define_global_function("require_relative", rb_f_require_relative, 1);
    rb_define_method(rb_cModule, "autoload", rb_mod_autoload, 2);
    rb_define_method(rb_cModule, "autoload?", rb_mod_autoload_p, 1);
    rb_define_global_function("autoload", rb_f_autoload, 2);
    rb_define_global_function("autoload?", rb_f_autoload_p, 1);
    
    ruby_dln_librefs = rb_ary_new();
    rb_gc_register_mark_object(ruby_dln_librefs);
}
