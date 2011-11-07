
#include<stdio.h>
#include<stdlib.h>
#include<assert.h>
#include<string.h>
#include<netinet/in.h>
#include<asm/types.h>
#include<sys/socket.h>
#include<arpa/inet.h>
		
#define VERSION "prefixtree v1.0"

int nodes = 0;

int dir = 0;
int lines = 0;

int batch = 0;

char intf[255];

#define DIR_UPLOAD      1
#define DIR_DOWNLOAD    2

#define TC_H_MAJ_MASK (0xFFFF0000U)
#define TC_H_MIN_MASK (0x0000FFFFU)
#define TC_H_MAJ(h) ((h)&TC_H_MAJ_MASK)
#define TC_H_MIN(h) ((h)&TC_H_MIN_MASK)
#define TC_H_MAKE(maj,min) (((maj)&TC_H_MAJ_MASK)|((min)&TC_H_MIN_MASK))


typedef struct node {

   struct node *children[256];

   unsigned char leafquad[256];
   __u32 leafclass[256];

   int tableid;

   unsigned int match_network;
   int match_mask;
   __u32 classid;

   unsigned char index;
   unsigned char subnum;
   unsigned char childnum;

   int netmask;
   char leaf;
} *pNode;


int get_tc_classid (__u32 *h, const char *str)
{
	__u32 maj, min;
	char *p;

	maj = strtoul(str, &p, 16);
	if (p == str) {
		maj = 0;
		if (*p != ':')
			return -1;
	}
	if (*p == ':') {
		if (maj >= (1<<16))
			return -1;
		maj <<= 16;
		str = p+1;
		min = strtoul(str, &p, 16);
		if (*p != 0)
			return -1;
		if (min >= (1<<16))
			return -1;
		maj |= min;
	} else if (*p != 0)
		return -1;

	*h = maj;
	return 0;
}


pNode nodeAlloc(int index, int tableid, int net, int mask, int classid, int netmask)
{
    pNode tmp;

    nodes++;

    tmp = (pNode) calloc(1, sizeof(struct node));
    assert(tmp!=NULL);

    if (800 == tableid) tableid = 8;

    tmp->tableid = tableid;
    tmp->match_network = net;
    tmp->match_mask = mask;
    tmp->leaf = 1;
    tmp->classid = classid;
    tmp->netmask = netmask;
    tmp->index = index;

    return tmp;
}


void addNode(pNode root, unsigned char b1, unsigned char b2, unsigned char b3, unsigned char b4,
    int mask, int classid)
{
    static int tableindex = 11;
    int size, i;

    pNode current = root;

    if(mask <= 8) {
        size = 1 << (8 - mask);
        for (i = b1; i < b1 + size; i++) {
            current->leafquad[i] = 1;
            current->leafclass[i] = classid;
            current->subnum++;
        }
        current->leaf = 1;
        return;
    }


    //Level 1
    if(current->children[b1] != NULL)
        current = current->children[b1];
    else {
        int net = (b1 << 24);
        current->children[b1] = nodeAlloc(b1, tableindex++, net, 0x00ff0000, classid, 8);
        current->childnum++;
        current->leaf = 0;
        current = current->children[b1];
    }

    if(mask > 8 && mask <= 16) {
        size = 1 << (16 - mask);
        for (i = b2; i < b2 + size; i++) {
            current->leafquad[i] = 1;
            current->leafclass[i] = classid;
            current->subnum++;
        }
        current->leaf = 1;
        return;
    }



    //Level 2
    if(current->children[b2] != NULL)
        current = current->children[b2];
    else {
        int net = (b1 << 24) | (b2 << 16)  ;
        current->children[b2] = nodeAlloc(b2, tableindex++, net, 0x0000ff00, classid, 16);
        current->childnum++;
        current->leaf = 0;
        current = current->children[b2];
    }

    if(mask > 16 && mask <= 24) {
        size = 1 << (24 - mask);
        for (i = b3; i < b3 + size; i++) {
            current->leafquad[i] = 1;
            current->leafclass[i] = classid;
            current->subnum++;
        }
        current->leaf = 1;
        return;
    }



    //Level 3
    if(current->children[b3] != NULL)
        current = current->children[b3];
    else {
        int net = (b1 << 24) | (b2 << 16) | (b3 << 8) ;
        current->children[b3] = nodeAlloc (b3, tableindex++, net, 0x000000ff, classid, 24);
        current->childnum++;
        current->leaf = 0;
        current = current->children[b3];
    }

    size = 1 << (32 - mask);
    for (i = b4; i < b4 + size; i++) {
        current->leafquad[i] = 1;
        current->leafclass[i] = classid;
        current->subnum++;
    }
    current->leaf = 1;
}


int readTree(pNode root, FILE *in)
{
    char linebuf[254];
    int b1,b2,b3,b4,m;
    __u32 classid;
    int ret;
    char strclass[256];
    int linecnt = 0;

    bzero(root->children, 256 * sizeof(struct node*));
    root->tableid = 10;
    root->leaf = 0;
    root->match_mask = 0xff000000;
    root->match_network = 0;


    while( fgets(linebuf, 200, in) != NULL) {
        ret = sscanf (linebuf, "%d.%d.%d.%d/%d %s", &b1, &b2, &b3, &b4, &m, strclass);
        if(ret != 6)
            printf("Parse error at line %d: %s", linecnt, linebuf);
        else if ( get_tc_classid (&classid, strclass) < 0 )
	    printf ("Invalid classid at line %d: %s", linecnt, linebuf);
	else addNode (root, b1, b2, b3, b4, m, classid);
	linecnt++;
    }
    
    lines = linecnt;
    
    return 0;
}


void printTree(pNode root)
{
    int i,j;
    static int level = 0;

    level++;

    for(i = 0;i < 256;i++)
        if(root->children[i]) {
                for(j = 0;j < level - 1;j++ )printf("\t");
                if(root->children[i]->leaf) {
                    printf("%d --->",i);
                    for(j=0;j<256;j++)
                        if(root->children[i]->leafquad[j]) printf(" |.%d [%d]", j, root->children[i]->leafclass[j]);
                    printf(" end\n");
                }
                 else printf ("%d \n",i);


            printTree(root->children[i]);
        }
	
    level--;
}


void writeTree(pNode root, FILE *out)
{
    int i,j;
    static int level = 0;
    struct in_addr addr;

    

    level++;

    if(root->childnum == 0) {

        for(i = 0;i < 256;i++)
            if(root->leafquad[i]) {
                for(j=0;j<level-1;j++) fprintf(out, "\t");
                fprintf(out, "%sfilter add dev %s protocol ip parent 1:0 prio 5 u32 ht %d:%x:  " \
                    "match ip %s 0.0.0.0/0 flowid %x:%x \n", batch ? "":"tc ", intf, root->tableid,i, (dir == 1) ? "src" : "dst",
                    TC_H_MAJ(root->leafclass[i])>>16, TC_H_MIN(root->leafclass[i]) );
            }

    } else {

        for(i = 0;i < 256;i++)
            if(root->children[i]) {
                addr.s_addr = ntohl(root->children[i]->match_network);
                for (j = 0;j < level - 1;j++) fprintf(out, "\t");
                fprintf(out, "\n## entries for %s/%d\n\n", inet_ntoa(addr), root->children[i]->netmask);
                for (j = 0;j < level - 1;j++) fprintf(out, "\t");
                fprintf(out, "%sfilter add dev %s parent 1:0 prio 5 handle %d: protocol ip u32 divisor 256\n", 
		    batch ? "":"tc ", intf, root->children[i]->tableid);
                for (j = 0;j < level - 1;j++) fprintf(out, "\t");
                fprintf(out, "%sfilter add dev %s protocol ip parent 1:0 prio 5 u32 ht %d:%x:  " \
                             "match ip %s 0.0.0.0/0 hashkey mask 0x%0x at %d link %d: \n\n",
			     batch ? "":"tc ", intf, root->tableid,i,
                            (dir == 1) ? "src" : "dst", root->children[i]->match_mask,
                            (dir == 1) ? 12 : 16, root->children[i]->tableid);

                writeTree(root->children[i], out);
            }

        if(root->subnum > 0) {
            for(i = 0;i < 256;i++)
                if(root->leafquad[i]) {
                    for(j = 0;j < level - 1;j++) fprintf(out, "\t");
                    fprintf(out, "%sfilter add dev %s protocol ip parent 1:0 prio 5 u32 ht %d:%x:  " \
                        "match ip %s 0.0.0.0/0 flowid %x:%x \n", batch ? "":"tc ", intf, root->tableid, i, (dir == 1) ? "src" : "dst",
                        TC_H_MAJ(root->leafclass[i])>>16, TC_H_MIN(root->leafclass[i]) );
                }
        }
    }

    level--;
}


void writeFilters(pNode root, FILE *out)
{

    fprintf(out, "##### Generated with %s #####\n\n", VERSION);

    fprintf(out, "%sfilter add dev %s parent 1:0 prio 5 protocol ip u32\n", batch ? "":"tc " ,intf);
    fprintf(out, "%sfilter add dev %s parent 1:0 prio 5 handle 10: protocol ip u32 divisor 256\n", batch ? "":"tc ", intf);

    fprintf(out, "%sfilter add dev %s protocol ip parent 1:0 prio 5 u32 ht 800:: " \
             "match ip %s 0.0.0.0/0 hashkey mask 0xff000000 at %d link 10: \n\n", batch ? "":"tc ", intf,
            (dir == 1) ? "src" : "dst", (dir == 1) ? 12 : 16 );


    writeTree(root, out);
}


int main (int argc, char **argv)
{
    FILE *in, *out;

    struct node root;
    bzero(&root, sizeof(struct node));

    if(argc < 5) {
	printf("IPv4 u32 hash filter generator - (C) 2006 Calin Velea\n\n");
        printf("Syntax: prefixtree {prefix.in} {u32filters.out} {interface} {src/dst} [batch]\n\n");
        return 1;
    }
    
    if (argc == 6)
	if(!strcmp(argv[5], "batch"))
	    batch = 1;
	    
    if(!strcmp(argv[4], "src"))
        dir = DIR_UPLOAD;
    else if(!strcmp(argv[4], "dst"))
        dir = DIR_DOWNLOAD;
    else {
        printf("Specify src or dst on the command line!\n");
        return 1;
    }
    
    strncpy(intf, argv[3], 255);

    in = fopen(argv[1],"r");
    out = fopen(argv[2],"w");
    
    if(in == NULL) {
        printf("Error opening: %s !\n", argv[1]);
        return 1;	
    }

    if(out == NULL) {
        printf("Error opening: %s !\n", argv[2]);
        return 1;	
    }
    
    readTree (&root, in);
    writeFilters (&root, out);

    printf("lines parsed: %d\n", lines);
    printf("total hashtables: %d\n", nodes);

    return 0;
}
