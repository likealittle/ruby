#include<stdio.h>
#include<stdlib.h>
#include<string.h>



struct file_tree_element{
    char *name;
    int file;
    struct file_tree_element *parent;
    struct file_tree_element *child;
    struct file_tree_element *right_sibling;
};

typedef struct file_tree_element file_tree_node;

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
int main(){
    file_tree_node *root;
    root = init_root(root);
    printf("%s\n",root -> name );
    file_tree_node *temp1,*temp2,*temp3;
    temp1 = get_new_node_str(temp1,"chetan");
    temp2 = get_new_node_str(temp2,"bademi");
    temp3 = get_new_node_str(temp3,"is awesome !");
    add_child(root,temp1);
    add_child(root,temp2);
    add_child(root,temp3);
    print_children(root);
    return 0;
}

