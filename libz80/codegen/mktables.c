/* =========================================================
 *  libz80 - Z80 emulation library
 * =========================================================
 *
 * (C) Gabriel Gambetta (gabriel.gambetta@gmail.com) 2000 - 2012
 *
 * Version 2.1.0
 *
 * ---------------------------------------------------------
 *
 * This utility generates most of the emulation code from the opcode specification
 *
 */
#include "stdio.h"
#include "regex.h"
#include "stdlib.h"
#include "string.h"
#include "ctype.h"


/* =========================================================
 *  Parameters
 * ========================================================= */
#define MAX_REGEX	1000		/**< Max patterns. */
#define MAX_LINES	20			/**< Max code lines per pattern. */
#define MAX_MATCH	5			/**< Max parameters per matching opcode. */
#define MAX_LINE	100			/**< Max line length */
#define OPCODE_OFFSET	11		/**< Offset of an opcode from the list, from the start of the line */

#define OPCODES_SPEC	"mktables.spec"
#define OPCODES_LIST	"opcodes.lst"
#define OPCODES_HEADER	"opcodes_decl.h"
#define OPCODES_IMPL	"opcodes_impl.c"
#define OPCODES_TABLE	"opcodes_table.h"


/* =========================================================
 *  Helpers
 * ========================================================= */
typedef unsigned char byte;

/** Print error and exit. */
void fatal(char* s)
{
	printf("%s\n", s);
	exit(1);	
}


/** Print error and exit. */
void fatal2(char* s, char* s2)
{
	printf("%s%s\n", s, s2);
	exit(1);	
}


/** Substitutes any of a set of characters for other character */
void substAny (char* str, char* set, char r)
{
	while (*str)
	{
		if (strchr(set, *str))
			*str = r;	
	
		str++;	
	}	
}


/** Substitutes a string for another */
void substStr(char* str, char* src, char* rep)
{
	char tmp[MAX_LINE];
	char tmp1[MAX_LINE];
	char* p;
	int l = strlen(src);
	
	strcpy(tmp, str);
	while ( (p = strstr(tmp, src)) != NULL)
	{
		*p = 0;
		strcpy(tmp1, tmp);
		strcat(tmp1, rep);
		strcat(tmp1, p + l);
		strcpy(tmp, tmp1);
	}
	
	strcpy(str, tmp);
}


/** Creates a function name from an opcode */
void fixName (char* opc, char* name)
{
	char* p, *q, *r;
	int l;
	
	strcpy(name, opc);
	substAny(name, " +,'", '_');
	substStr(name, "(", "off_");
	substStr(name, ")", "");
}


/** Removes trailing CR and LF. */
void trim (char* s)
{
	while (*s)
	{
		if ((*s == 0x0D) || (*s == 0x0A))	
			*s = 0;
		s++;
	}
}


/** Opens a file or dies trying */
FILE* openOrDie(char* name, char* mode)
{
	FILE* file = fopen(name, mode);
	if (!file)
		fatal2("Can't open ", name);	
	
	return file;
}

/* =========================================================
 *  Specification structures
 * ========================================================= */

/** A pattern and its substitution. */
typedef struct
{
	regex_t	re;
	char*	pat;
	char*	line[MAX_LINES];	
} Item;


Item*	items;		/**< The patterns. */
int		nItems;		/**< Number of defined patterns. */


/** Reads the specification file. */
int readSpec (FILE* in)
{
	char line[MAX_LINE];
	int nLine;
	
	items = calloc(MAX_REGEX, sizeof(Item));
	if (!items)
		fatal("Not enough memory");
		
	nItems = -1;
	do
	{
		if (feof(in))
			break;
			
		fgets(line, MAX_LINE, in);
		trim(line);
		if (line[0] == '#')	/* Comment */
		{
			
		}
		else if (line[0] != '\t')	/* New pattern definition */
		{		
			strcat(line, "$");
			nItems++;
			if (regcomp(&items[nItems].re, line, REG_EXTENDED))
				fatal(line);
			nLine = 0;
			items[nItems].pat = strdup(line);
		}
		else	/* A line in the current pattern */
		{
			if (strlen(line) > 0)
				items[nItems].line[nLine++] = strdup(line);
		}
	} while(1);
	
	nItems++;
	
	return nItems+1;
}



/** Prints an item pattern and its substitution */
void printItem (Item* item)
{
	char** s = item->line;
	
	printf("%s\n", item->pat);
	while (*s)
	{
		printf("[%s]\n", *s);
		s++;
	}
}


/* =========================================================
 *  Opcode implementation and declaration generator
 * ========================================================= */

/** Handles opcode-in-code */
int printCall (char* s, FILE* code)
{
	char tmp[MAX_LINE];	
	char* p = tmp;
	
	strcpy(tmp, s);
	
	while (isspace(*p) && (*p))
		p++;
		
	if (*p != '%')
		return 0;
	
	*p = 0;
	fprintf(code, "%s", tmp);
	p++;
	
	fixName(&s[p-tmp], tmp);
	fprintf(code, "%s(ctx);\n", tmp);
	return 1;
}


/** Reads the opcode list and generates output code based on the spec */
void generateCodeTable (FILE* opcodes, FILE* code)
{
	char line[MAX_LINE];
	char last[MAX_LINE];
	char tmp[MAX_LINE];
	char name[MAX_LINE];
	char parm[5], subst[20];
	int i;
	char* p, *q;
	char** cmds;
	regmatch_t matches[MAX_MATCH];
	Item* item;

	printf("Generating opcode implementations...");
	last[0] = 0;
	do
	{			
		fgets(line, MAX_LINE, opcodes);
		trim(line);
		
		if (feof(opcodes))
			break;
				
		for (q = line, p = q+OPCODE_OFFSET; *p; *q++ = *p++);	/* Skip the hex part */
		*q = 0;
		
		/* Avoid duplicate opcodes */
		if (strcmp(last, line) == 0)
			continue;		
		strcpy(last, line);
			
		/* Find the appropriate pattern */
		for (i = 0; i < nItems; i++)
		{
			if (regexec(&items[i].re, line, MAX_MATCH, matches, REG_EXTENDED) == 0)
			{	
				if (matches[0].rm_so == 0)	/* Match only at beginning of line */
				{
					/*printf("%s : match %s\n", &line, items[i].pat);*/
					break;
				}
			}
		}
		
		if (i >= nItems)
			fatal2(line, " didn't match anything");
			
		item = &items[i];
		
		/* Print function stub */
		fixName(line, name);
		fprintf(code, "static void %s (Z80Context* ctx)\n{\n", name);
				
		/* Substitute submatches in each output line and print the code */
		cmds = item->line;
		strcpy(parm, "%0");
		while (*cmds)
		{
			if (!printCall(*cmds, code))
			{
				strncpy(tmp, *cmds, MAX_LINE);
				q = tmp;
		
				for (i = 1; i < MAX_MATCH; i++)
				{
					parm[1] = i + '0';
					strncpy(subst, &line[matches[i].rm_so], matches[i].rm_eo - matches[i].rm_so);
					subst[matches[i].rm_eo - matches[i].rm_so] = 0;
					
					substStr(tmp, parm, subst);
				}
				fprintf(code, "%s\n", tmp);
			}
			
			cmds++;	
		}
		
		fprintf(code, "}\n\n\n");
	} while(1);
	printf("done\n");
}


void generateCode (void)
{
	FILE* spec, *opcodes, *code, *table;
	
	/* ----- Read the specification ----- */
	printf("Reading specification...");

	spec = openOrDie(OPCODES_SPEC, "rb");
	if (!readSpec(spec))
		fatal("Error reading the specification");	
	fclose(spec);
	printf("done\n");

	opcodes = openOrDie(OPCODES_LIST, "rb");
	code = openOrDie(OPCODES_IMPL, "wb");
		
	generateCodeTable(opcodes, code);
	
	fclose(opcodes);
	fclose(code);
}


/* =========================================================
 *  Parser tables generator
 * ========================================================= */

void mkFormat(char* src, char* dst)
{
	strcpy(dst, src);
	substStr(dst, "d", "0%02Xh");
	substStr(dst, "e", "%d");
	substStr(dst, "nn", "0%04Xh");
	substStr(dst, "n", "0%02Xh");	
}


int isHex(char c)
{
	return (isxdigit(c) && isupper(c)) || isdigit(c);	
}


int hexVal(char c)
{
	if (isdigit(c))
		return c - '0';
	return c - 'A' + 10;	
}


/* ---------------------------------------------------------
 *  Parser structures. Not exactly the same as in z80.c
 * --------------------------------------------------------- */
typedef enum
{
	OP_NONE,
	OP_BYTE,
	OP_OFFSET,
	OP_WORD	
} Z80OperandType;

char* OpTypeName[] = { "OP_NONE", "OP_BYTE", "OP_OFFSET", "OP_WORD" };

struct Z80OpcodeTable;

struct Z80OpcodeEntry
{
	int pre_skip;
	int post_skip;
	char* func;		/*	Z80OpcodeFunc* func;*/
	
	int operand_type;
	char* format;	
	
	struct Z80OpcodeTable* table;
};


struct Z80OpcodeTable
{
	char* name;
	int opcode_offset;
	struct Z80OpcodeEntry entries[256];
};


typedef enum
{
	TT_OPCODE,
	TT_D,
	TT_E,
	TT_N,
	TT_NN,
	TT_END
} TokenType;


char* nextToken(char* cur, byte* val, TokenType* ttype)
{
	*ttype = TT_END;
	while (*cur)
	{
		if (*cur == ' ')
		{
			cur++;
			continue;
		}
		else if (isHex(*cur) && isHex(*(cur+1)))
		{
			*val = (hexVal(*cur) << 4) | hexVal(*(cur+1));
			*ttype = TT_OPCODE;
			cur += 2;
			break;
		}
		else if (*cur == 'd')
		{
			*ttype = TT_D;	
			cur++;
			break;
		}
		else if (*cur == 'e')
		{
			*ttype = TT_E;
			cur++;
			break;
		}
		else if ((*cur == 'n') && (*(cur+1) == 'n'))
		{
			*ttype = TT_NN;
			cur += 2;
			break;
		}
		else if (*cur == 'n')
		{
			*ttype = TT_N;
			cur++;
			break;
		}
		else
		{
			fatal2("Unknown char	at ", cur);
		}
	};
	
	return cur;
}


/** Creates the table tree implied by the prefixes */
struct Z80OpcodeTable* createTableTree(FILE* opcodes, FILE* table)
{
	struct Z80OpcodeTable* mainTable;
	struct Z80OpcodeTable* current;
	char line[MAX_LINE];
	char tmp[MAX_LINE];
	char* cur;
	byte code;
	TokenType tt;	
	byte prefix[10];
	int prefixLen, i;
	int wasOperand;
	int opcodeOffset;
	
	mainTable = calloc(1, sizeof(struct Z80OpcodeTable));
	mainTable->name = strdup("");
	
	fprintf(table, "static const struct Z80OpcodeTable opcodes_main;\n");
	
	rewind(opcodes);

	printf("Determining opcode prefixes...");
	do
	{
		fgets(line, MAX_LINE, opcodes);
		trim(line);
		
		if (feof(opcodes))
			break;
			
		prefixLen = 0;
		line[OPCODE_OFFSET - 1] = 0;
	
		/* Get the codes */	
		wasOperand = 0;
		opcodeOffset = 0;
		cur = line;
		do
		{
			cur = nextToken(cur, &code, &tt);
			if (tt == TT_END)
			{
				break;
			}
			else if (tt == TT_OPCODE)
			{
				prefix[prefixLen++] = code;
				if (wasOperand)
					opcodeOffset++;
			}
			else
			{
				wasOperand = 1;	
			}
		} while(1);
	
		/* All hex codes except the last one form the prefix of the instruction */
		current = mainTable;
		for (i = 0; i < prefixLen - 1; i++)
		{
			code = prefix[i];
			/* Create a new node if it doesn't exist */
			if (current->entries[code].table == NULL)
			{
				sprintf(tmp, "%s%02X", current->name, code);
				current->entries[code].table = calloc(1, sizeof(struct Z80OpcodeTable));
				current = current->entries[code].table;
				current->name = strdup(tmp);				
				fprintf(table, "static const struct Z80OpcodeTable opcodes_%s;\n", current->name);
				
				printf("%s ", tmp);
			}
			else
			{
				current = current->entries[code].table;
			}
		}
		
		current->opcode_offset = opcodeOffset;
	} while(1);
	
	printf("\n");
	
	mainTable->name = strdup("main");
	return mainTable;
}


void scanOpcodes(FILE* opcodes, struct Z80OpcodeTable* mainTable)
{
	char line[MAX_LINE];
	char name[MAX_LINE];
	char fmt[MAX_LINE];
	char* cur;
	byte code;
	TokenType tt;
	struct Z80OpcodeTable* current;
	struct Z80OpcodeEntry* ent;
	int opType;
	
	rewind(opcodes);	
	do
	{
		fgets(line, MAX_LINE, opcodes);
		trim(line);
		
		if (feof(opcodes))
			break;
			
		mkFormat(&line[OPCODE_OFFSET], fmt);
		fixName(&line[OPCODE_OFFSET], name);
		line[OPCODE_OFFSET] = 0;
	
		current = mainTable;		
		cur = line;
		opType = OP_NONE;
		do
		{
			cur = nextToken(cur, &code, &tt);
			if (tt == TT_END)
			{
				break;
			}
			else if (tt == TT_OPCODE)
			{
				if (current->entries[code].table)
				{
					current = current->entries[code].table;
					continue;
				}
				
				ent = &current->entries[code];
				ent->func = strdup(name);
				ent->format = strdup(fmt);				
			}
			else if (tt == TT_NN)
			{
				opType = OP_WORD;
			}
			else if ((tt == TT_N) | (tt == TT_D))
			{
				opType = OP_BYTE;
			}
			else if (tt == TT_E)
			{
				opType = OP_OFFSET;
			}
		} while(1);
		
		ent->operand_type = opType;
			
	} while (1);
}


void outputTable(struct Z80OpcodeTable* table, FILE* file)
{
	int i;
	struct Z80OpcodeEntry* opc;
	struct Z80OpcodeTable* tbl;
	char fmt[MAX_LINE];
	
	printf("Outputting table %s...", table->name);
	
	fprintf(file, "static const struct Z80OpcodeTable opcodes_%s = { %d, {\n", table->name, table->opcode_offset);
	
	for (i = 0, opc = table->entries; i < 256; i++, opc++)
	{
		tbl = opc->table;

		strcpy(fmt, "NULL");
		if (opc->format)
			sprintf(fmt, "\"%s\"", opc->format);

		fprintf(file, "\t{ %-20s, %-9s, %-20s, %s%s }%s\n",
												(opc->func ? opc->func : "NULL"),
												OpTypeName[opc->operand_type],
												fmt,
												(tbl ? "&opcodes_" : ""),
												(tbl ? tbl->name : "NULL"),
												(i == 255 ? "" : ",")
												);

	}
	fprintf(file, "} };\n");	
	fprintf(file, "\n\n");
		
	printf("done\n");
	
	for (i = 0, opc = table->entries; i < 256; i++, opc++)
	{
		tbl = opc->table;			
		if (tbl)
			outputTable(tbl, file);
	}
}
	

void generateParserTables(FILE* opcodes, FILE* table)
{
	struct Z80OpcodeTable* mainTable = createTableTree(opcodes, table);
	scanOpcodes(opcodes, mainTable);
	fprintf(table, "\n\n");
	outputTable(mainTable, table);
}


void generateParser(void)
{
	FILE* table, *opcodes;
	
	opcodes = openOrDie(OPCODES_LIST, "rb");
	table = openOrDie(OPCODES_TABLE, "wb");
	
	generateParserTables(opcodes, table);
	
	fclose(table);
	fclose(opcodes);
}


int main (void)
{
	generateCode();
	generateParser();
	return 0;
}

