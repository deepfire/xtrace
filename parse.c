/*  This file is part of "xtrace"
 *  Copyright (C) 2005 Bernhard R. Link
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */
#include <config.h>

#include <assert.h>
#include <values.h>
#include <sys/types.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/select.h>
#include <unistd.h>

#include "xtrace.h"


static inline unsigned int padded(unsigned int s) {
	return (s+3)&(~3);
}

#define CARD16(bigendian,buffer,ofs) ((bigendian)?(buffer[ofs]*256+buffer[ofs+1]):(buffer[ofs+1]*256+buffer[ofs]))
#define CARD32(bigendian,buffer,ofs) ((bigendian)?(((buffer[ofs]*256+buffer[ofs+1])*256+buffer[ofs+2])*256+buffer[ofs+3]):(buffer[ofs]+256*(buffer[ofs+1]+256*(buffer[ofs+2]+256*buffer[ofs+3]))))
#define clientCARD32(ofs) CARD32(c->bigendian,c->clientbuffer,ofs)
#define clientCARD16(ofs) CARD16(c->bigendian,c->clientbuffer,ofs)
#define clientCARD8(ofs) c->clientbuffer[ofs]
#define serverCARD32(ofs) CARD32(c->bigendian,c->serverbuffer,ofs)
#define serverCARD16(ofs) CARD16(c->bigendian,c->serverbuffer,ofs)
#define serverCARD8(ofs) c->serverbuffer[ofs]
#define getCARD32(ofs) CARD32(bigendian,buffer,ofs)
#define getCARD16(ofs) CARD16(bigendian,buffer,ofs)
#define getCARD8(ofs) buffer[ofs]
#define getCARD32(ofs) CARD32(bigendian,buffer,ofs)

#define getBE32(ofs) (((buffer[ofs]*256+buffer[ofs+1])*256+buffer[ofs+2])*256+buffer[ofs+4])

#define NUM(array) (sizeof(array)/sizeof(array[0]))

struct constant {
	unsigned long value;
	const char *name;
};

static void print_bitfield(const char *name,const struct constant *constants, unsigned long l){
	const struct constant *c;
	const char *zeroname = "0";
	bool first = true;

	/* bitmasks should have some */
	assert(constants != NULL);
	printf("%s=",name);

	for( c = constants; c->name != NULL ; c++ ) {
		if( c->value == 0 )
			zeroname = c->name;
		else if( (l & c->value) != 0 ) {
			if( !first )
				putchar(',');
			first = false;
			fputs(c->name,stdout);
		}
		
	}
	if( first )
		fputs(zeroname,stdout);
};

static const char *findConstant(const struct constant *constants, unsigned long l){
	const struct constant *c;

	if( constants == NULL )
		return NULL;

	for( c = constants; c->name != NULL ; c++ ) {
		if( c->value == l )
			return c->name;
	}
	return NULL;
};

#define OFS_LATER ((size_t)-1)

struct parameter {
	/* The offset within the event, request, reply or Struct this
	 * applies to. If OFS_LATER it is after the last list item
	 * in this parameter-list. */
	size_t offse;
	const char *name;
	enum fieldtype { 
		/* signed endian specific: */
		ft_INT8, ft_INT16, ft_INT32,
		/* unsigned decimal endian specific: */
		ft_UINT8, ft_UINT16, ft_UINT32,
		/* unsigned hex endian specific: */
		ft_CARD8, ft_CARD16, ft_CARD32,
		/* enums (not in constant list is error): */
		ft_ENUM8, ft_ENUM16, ft_ENUM32,
		/* counts for following strings, lists, ...
		 * value-mask for LISTofFormat */
		ft_STORE8, ft_STORE16, ft_STORE32,
		/* bitfields: multiple values are possible */
		ft_BITMASK8, ft_BITMASK16, ft_BITMASK32,
		/* Different forms of lists: */
		/*	- boring ones */
		ft_STRING8, ft_LISTofCARD32, 
		ft_LISTofCARD8, ft_LISTofCARD16, 
		/*	- one of the above depening on last FORMAT */
		ft_LISTofFormat,
		/*	- iterate of list description in constants field */
		ft_LISTofStruct, 
		/*	- same but length is mininum length and
		 *	  actual length is taken from end of last list
		 *	  or LASTMARKER */
		ft_LISTofVarStruct,
		/*	- like ENUM for last STORE, but constants
		 *	  are of type (struct value*) interpreteted at this
		 *	  offset */
		ft_LISTofVALUE,
		/* an LISTofStruct with count = 1 */
		ft_Struct, 
		/* specify bits per item for LISTofFormat */
		ft_FORMAT8,
		/* an event 
		 * (would have also been possible with Struct and many IF)*/
		ft_EVENT, 
		/* jump to other parameter list if matches */
		ft_IF8, 
		/* set end of last list manually, (for LISTofVarStruct) */
		ft_LASTMARKER, 
		/* always big endian */
		ft_BE32
		} type;
	const struct constant *constants;
};

typedef const unsigned char u8;

static size_t printSTRING8(bool bigendian,u8 *buffer,size_t buflen,const struct parameter *p,size_t len,size_t ofs){
	size_t nr = 0;

	if( buflen < ofs )
		return ofs;
	if( buflen - ofs <= len )
		len = buflen - ofs;

	printf("%s='",p->name);
	while( len > 0 ) {
		if( nr == maxshownlistlen ) {
			fputs("'...",stdout);
		} else if( nr < maxshownlistlen ) {
			if( getCARD8(ofs)< ' ' || getCARD8(ofs) > 'z' )
				printf("\\%03hho",getCARD8(ofs));
			else
				putchar(getCARD8(ofs));
		}
		ofs++;len--;nr++;
	}
	if( nr <= maxshownlistlen )
		putchar('\'');
	return ofs;
}

static size_t printLISTofCARD8(bool bigendian,u8 *buffer,size_t buflen,const struct parameter *p,size_t len, size_t ofs){
	bool notfirst = false;
	size_t nr = 0;

	if( buflen < ofs )
		return ofs;
	if( buflen - ofs <= len )
		len = buflen - ofs;

	printf("%s=",p->name);
	while( len > 0 ) {
		const char *value;
		unsigned char u8;

		if( nr == maxshownlistlen ) {
			fputs(",...",stdout);
		} else if( nr < maxshownlistlen ) {
			if( notfirst )
				putchar(',');
			notfirst = true;
			u8 = getCARD8(ofs);
			value = findConstant(p->constants,u8);
			if( value )
				printf("%s(0x%hhx)",value,u8);
			else
				printf("0x%02hhx",u8);
		}
		len--;ofs++;nr++;
	}
	putchar(';');
	return ofs;
}

static size_t printLISTofCARD16(bool bigendian,u8 *buffer,size_t buflen,const struct parameter *p,size_t len, size_t ofs){
	bool notfirst = false;
	size_t nr = 0;

	if( buflen < ofs )
		return ofs;
	if( (buflen - ofs)/2 <= len )
		len = (buflen - ofs)/2;

	printf("%s=",p->name);
	while( len > 0 ) {
		const char *value;
		u_int16_t u16;

		if( nr == maxshownlistlen ) {
			fputs(",...",stdout);
		} else if( nr < maxshownlistlen ) {
			if( notfirst )
				putchar(',');
			notfirst = true;
			u16 = getCARD16(ofs);
			value = findConstant(p->constants,u16);
			if( value )
				printf("%s(0x%hx)",value,u16);
			else
				printf("0x%04hx",u16);
		}
		len--;ofs+=2;nr++;
	}
	putchar(';');
	return ofs;
}

static size_t printLISTofCARD32(bool bigendian,u8 *buffer,size_t buflen,const struct parameter *p,size_t len, size_t ofs){
	bool notfirst = false;
	size_t nr = 0;

	if( buflen < ofs )
		return ofs;
	if( (buflen - ofs)/4 <= len )
		len = (buflen - ofs)/4;

	printf("%s=",p->name);
	while( len > 0 ) {
		const char *value;
		u_int32_t u32;

		if( nr == maxshownlistlen ) {
			fputs(",...",stdout);
		} else if( nr < maxshownlistlen ) {
			if( notfirst )
				putchar(',');
			notfirst = true;
			u32 = getCARD32(ofs);
			value = findConstant(p->constants,u32);
			if( value )
				printf("%s(0x%x)",value,u32);
			else
				printf("0x%08x",u32);
		}
		len--;ofs+=4;nr++;
	}
	putchar(';');
	return ofs;
}

struct value {
	unsigned long flag;
	/* NULL means EndOfValues */
	const char *name;
	/* only elementary type (<= ft_BITMASK32 are allowed ), */
	enum fieldtype type;
	const struct constant *constants;
};

static size_t printLISTofVALUE(bool bigendian,u8 *buffer,size_t buflen,const struct parameter *param,unsigned long valuemask, size_t ofs){

	const struct value *v = (const struct value*)param->constants;
	bool notfirst = false;

	assert( v != NULL );

	if( ofs > buflen )
		return ofs;
	printf("%s={",param->name);
	while( buflen > ofs && buflen-ofs >= 4 ) {
		u_int32_t u32; u_int16_t u16; u_int8_t u8;
		const char *constant;

		if( v->name == NULL ) /* EOV */
			break;
		if( (valuemask & v->flag) == 0 ) {
			v++;
			continue;
		}
		if( notfirst )
			putchar(' ');
		notfirst = true;
		/* this is funny, but that is the protocol... */
		u32 = getCARD32(ofs);
		u16 = u32 & 65535;
		u8 = u32 & 255;
		if( v->type >= ft_BITMASK8 ) {
			assert(v->type <= ft_BITMASK32 );
			print_bitfield(v->name,v->constants,u32);
			ofs += 4;v++;
			continue;
		}
		assert( v->type < ft_STORE8 );
		switch( v->type % 3 ) {
		 case 0:
			constant = findConstant(v->constants,u8);
			break;
		 case 1:
			constant = findConstant(v->constants,u16);
			break;
		 default:
			constant = findConstant(v->constants,u32);
			break;
		}
		fputs(v->name,stdout);putchar('=');
		if( constant != NULL ) {
			fputs(constant,stdout);
			putchar('(');
		}
		switch( v->type ) {
		 case ft_INT8:
			 printf("%hhd",u8);
			 break;
		 case ft_INT16:
			 printf("%hd",u16);
			 break;
		 case ft_INT32:
			 printf("%d",u32);
			 break;
		 case ft_UINT8:
			 printf("%hhu",u8);
			 break;
		 case ft_UINT16:
			 printf("%hu",u16);
			 break;
		 case ft_UINT32:
			 printf("%u",u32);
			 break;
		 case ft_ENUM8:
			 if( constant == NULL )
				 fputs("unknown:",stdout);
		 case ft_CARD8:
			 printf("0x%02hhx",u8);
			 break;
		 case ft_ENUM16:
			 if( constant == NULL )
				 fputs("unknown:",stdout);
		 case ft_CARD16:
			 printf("0x%04hx",u16);
			 break;
		 case ft_ENUM32:
			 if( constant == NULL )
				 fputs("unknown:",stdout);
		 case ft_CARD32:
			 printf("0x%08x",u32);
			 break;
		 default:
			 assert(0);
		}
		if( constant != NULL ) {
			putchar(')');
		}
		ofs += 4; v++;
	}
	putchar('}');
	/* TODO: print error if flags left or v!=EOV? */
	return ofs;
}

static size_t print_parameters(bool bigendian,const unsigned char *buffer,unsigned int len, const struct parameter *parameters, bool bigrequest);

static size_t printLISTofStruct(bool bigendian,u8 *buffer,size_t buflen,const struct parameter *p,size_t count, size_t ofs){
	bool notfirst = false;
//	size_t ofs = (p->offset<0)?lastofs:p->offset;
	/* This is a gross hack: the constants for ft_LISTofStruct are
	 * in reality a parameter structure */
	const struct parameter *substruct = (const void*)p->constants;
	size_t len;
	size_t nr = 0;

	/* and the first item includes the length of an item */
	assert( substruct != NULL && substruct->name == NULL && substruct->offse > 0);
	len = substruct->offse;
	substruct++;

	printf("%s=",p->name);
	while( buflen > ofs && buflen-ofs >= len && count > 0) {

		if( nr == maxshownlistlen ) {
			fputs(",...",stdout);
			if( len == 0 )
				ofs = SIZE_MAX;
			break;
		} else if( nr < maxshownlistlen ) {
			if( notfirst )
				putchar(',');
			notfirst = true;
			putchar('{');

			print_parameters(bigendian,buffer+ofs,len,substruct,false);

			putchar('}');
		}
		ofs += len; count--; nr++;
	}
	putchar(';');
	return ofs;
}
static size_t printLISTofVarStruct(bool bigendian,u8 *buffer,size_t buflen,const struct parameter *p,size_t count, size_t ofs){
	bool notfirst = false;
//	size_t ofs = (p->offset<0)?lastofs:p->offset;
	const struct parameter *substruct = (const void*)p->constants;
	size_t len;
	size_t nr = 0;

	/* in this case this is only the minimum value */
	assert( substruct != NULL && substruct->name == NULL && substruct->offse > 0);
	len = substruct->offse;
	substruct++;

	printf("%s=",p->name);
	while( buflen > ofs && buflen-ofs >= len && count > 0) {
		size_t lentoadd;

		if( nr >= maxshownlistlen ) {
			fputs(",...;",stdout);
			/* there is nothing here to calculate the rest,
			 * so just return the unreachable */
			return SIZE_MAX;
		}
		if( notfirst )
			putchar(',');
		notfirst = true;
		putchar('{');

		lentoadd = print_parameters(bigendian,buffer+ofs,len,substruct,false);

		putchar('}');
		ofs += lentoadd; count--; nr++;
	}
	putchar(';');
	return ofs;
}

/* buffer must have at least 32 valid bytes */
static void print_event(bool bigendian,const unsigned char *buffer);

static size_t print_parameters(bool bigendian,const unsigned char *buffer,unsigned int len, const struct parameter *parameters,bool bigrequest) {
	const struct parameter *p;
	unsigned long stored = INT_MAX;
	unsigned char format = 0;
	bool notfirst = false;
	size_t lastofs = 0;

	for( p = parameters; p->name != NULL; p++ ) {
#ifdef STUPIDCC
		u_int8_t u8=0; u_int16_t u16=0; u_int32_t u32=0;
		unsigned long l=0;
#else
		u_int8_t u8; u_int16_t u16; u_int32_t u32;
		unsigned long l;
#endif
		size_t ofs;
		const char *value;

		if( p->offse == OFS_LATER )
			ofs = lastofs;
		else if( bigrequest && p->offse >= 4 )
			/* jump over 32 bit extended length */
			ofs = p->offse+4;
		else
			ofs = p->offse;

		if( notfirst )
			putchar(' ');
		notfirst = true;

		if( p->type == ft_IF8 ) {
			if( ofs < len && 
			  /* some more overloading: */
			  getCARD8(ofs) == (unsigned char)(p->name[0]) )
				p = (struct parameter *)p->constants;
			else
				continue;
		}

		switch( p->type ) {
		 case ft_LASTMARKER:
			 lastofs = ofs;
			 continue;
		 case ft_FORMAT8:
			 if( ofs < len )
				 format = getCARD8(ofs);
			 continue;
		 case ft_STRING8:
			lastofs = printSTRING8(bigendian,buffer,len,p,stored,ofs);
			continue;
		 case ft_LISTofCARD8:
			lastofs = printLISTofCARD8(bigendian,buffer,len,p,stored,ofs);
			continue;
		 case ft_LISTofCARD16:
			lastofs = printLISTofCARD16(bigendian,buffer,len,p,stored,ofs);
			continue;
		 case ft_LISTofCARD32:
			lastofs = printLISTofCARD32(bigendian,buffer,len,p,stored,ofs);
			continue;
		 case ft_LISTofFormat:
			switch( format ) {
			 case 8:
				lastofs = printLISTofCARD8(bigendian,buffer,len,p,stored,ofs);
				break;
			 case 16:
				lastofs = printLISTofCARD16(bigendian,buffer,len,p,stored,ofs);
				break;
			 case 32:
				lastofs = printLISTofCARD32(bigendian,buffer,len,p,stored,ofs);
				break;
			 default:
				lastofs = ofs;
				break;
			}
			continue;
		 case ft_Struct:
			printLISTofStruct(bigendian,buffer,len,p,1,ofs);
			continue;
		 case ft_LISTofStruct:
			lastofs = printLISTofStruct(bigendian,buffer,len,p,stored,ofs);
			continue;
		 case ft_LISTofVarStruct:
			lastofs = printLISTofVarStruct(bigendian,buffer,len,p,stored,ofs);
			continue;
		 case ft_LISTofVALUE:
			lastofs = printLISTofVALUE(bigendian,buffer,len,p,stored,ofs);
			continue;
		 case ft_EVENT:
			if( len >= ofs + 32 )
				print_event(bigendian,buffer+ofs);
			continue;
		 case ft_BE32:
			if( ofs + 4 > len )
				continue;
			fputs(p->name,stdout);putchar('=');
			printf("0x%08x",(unsigned int)getBE32(ofs));
			continue;
		 default:
			break;
		}
		assert( p->type <= ft_BITMASK32);
		 
		if( ((ofs+4)&~3) > len )
			/* this field is missing */
			continue;
		switch( p->type % 3) {
		 case 0:
			 u8 = getCARD8(ofs);
			 l = u8;
			 break;
		 case 1:
			 u16 = getCARD16(ofs);
			 l = u16;
			 break;
		 case 2:
			 u32 = getCARD32(ofs);
			 l = u32;
			 break;
		}
		if( p->type >= ft_BITMASK8 ) {
			assert(p->type <= ft_BITMASK32 );
			print_bitfield(p->name,p->constants,l);
			continue;
		}
		if( p->type >= ft_STORE8 ) {
			assert(p->type <= ft_STORE32);
			stored = l;
			continue;
		}
		value = findConstant(p->constants,l);
		fputs(p->name,stdout);putchar('=');
		if( value != NULL ) {
			fputs(value,stdout);
			putchar('(');
		}
		switch( p->type ) {
		 case ft_INT8:
			 printf("%hhd",u8);
			 break;
		 case ft_INT16:
			 printf("%hd",u16);
			 break;
		 case ft_INT32:
			 printf("%d",u32);
			 break;
		 case ft_UINT8:
			 printf("%hhu",u8);
			 break;
		 case ft_UINT16:
			 printf("%hu",u16);
			 break;
		 case ft_UINT32:
			 printf("%u",u32);
			 break;
		 case ft_ENUM8:
			 if( value == NULL )
				 fputs("unknown:",stdout);
		 case ft_CARD8:
			 printf("0x%02hhx",u8);
			 break;
		 case ft_ENUM16:
			 if( value == NULL )
				 fputs("unknown:",stdout);
		 case ft_CARD16:
			 printf("0x%04hx",u16);
			 break;
		 case ft_ENUM32:
			 if( value == NULL )
				 fputs("unknown:",stdout);
		 case ft_CARD32:
			 printf("0x%08x",u32);
			 break;
		 case ft_STORE8:
		 case ft_STORE16:
		 case ft_STORE32:
		 case ft_BITMASK8:
		 case ft_BITMASK16:
		 case ft_BITMASK32:
		 case ft_STRING8:
		 case ft_FORMAT8:
		 case ft_LISTofCARD8:
		 case ft_LISTofCARD16:
		 case ft_LISTofCARD32:
		 case ft_LISTofFormat:
		 case ft_LISTofVALUE:
		 case ft_Struct:
		 case ft_LISTofStruct:
		 case ft_LISTofVarStruct:
		 case ft_IF8:
		 case ft_BE32:
		 case ft_LASTMARKER:
		 case ft_EVENT:
			 assert(0);
		}
		if( value != NULL ) {
			putchar(')');
		}
	}
	return lastofs;
}

typedef bool request_func(struct connection*,bool,bool);

/* replace ra(GrabButton) in requests.inc by ra2(GrabButton)
 * and add this function and all
 * GrabButton requests will have an AnyModifier set before
 * being forwarded to the server... >:-] 
 *
static bool requestGrabButton(struct connection *c, bool pre, bool bigrequest) {
	if( !pre )
		return false;
	if( c->bigendian )
		c->clientbuffer[22] |= 0x80;
	else
		c->clientbuffer[23] |= 0x80;
	return false;
}

*/

/* Reactions to some replies */

typedef void reply_func(struct connection*,bool*,bool*);

static void replyListFontsWithInfo(struct connection *c,bool *ignore,bool *dontremove) {
	unsigned int seq = serverCARD16(2);
	if( serverCARD8(1) == 0 ) {

		printf("%03d:>:0x%04x:%u: Reply to ListFontsWithInfo: end of list\n", c->id, seq, c->serverignore);
		*ignore = true;
	} else
		*dontremove = true;
}
static void replyQueryExtension(struct connection *c,bool *ignore,bool *dontremove) {
	if( denyallextensions ) {
		/* disable all extensions */
		c->serverbuffer[8] = 0;
	}
}

struct request { 
	const char *name;
	const struct parameter *parameters;
	const struct parameter *answers;

	request_func *request_func;
	reply_func *reply_func;
};

#define ft_COUNT8 ft_STORE8
#define ft_COUNT16 ft_STORE16
#define ft_COUNT32 ft_STORE32
#include "requests.inc"

struct expectedreply {
	struct expectedreply *next;
	u_int64_t seq;
	const struct request *from;
};

static inline void free_expectedreplylist(struct expectedreply *r) {

	while( r != NULL ) {
		struct expectedreply *n = r->next;
		free(r);
		r = n;
	}
}

static inline void print_client_request(struct connection *c,bool bigrequest) {
	unsigned char req = clientCARD8(0);
	const struct request *r;
	size_t len;
	bool ignore;

	len = c->clientignore;
	if( len > c->clientcount )
		len = c->clientcount;

	if( req < NUM(requests) )
		r = &requests[req];
	else r = &requests[0];
	c->seq++;
	if( r->request_func == NULL )
		ignore = false;
	else
		ignore = r->request_func(c,true,bigrequest);
	if( !ignore ) {
		printf("%03d:<:%04x:%3u: Request(%hhu): %s ",
				c->id,(unsigned int)(c->seq),c->clientignore,req,
				r->name
		      );
		print_parameters(c->bigendian,c->clientbuffer,len,r->parameters, bigrequest);
		if( r->request_func != NULL )
			(void)r->request_func(c,false,bigrequest);
		putchar('\n');
	}
	if( r->answers != NULL ) {
		/* register an awaited response */
		struct expectedreply *a = malloc(sizeof(struct expectedreply));
		if( a == NULL )
			abort();
		a->next = c->expectedreplies;
		a->seq = c->seq;
		a->from = r;
		c->expectedreplies = a;
	}
}

static inline void print_server_event(struct connection *c) {

	printf("%03d:>:%04llx: Event ",c->id,c->seq);
	print_event(c->bigendian,c->serverbuffer);
	putchar('\n');
}


static inline void print_server_reply(struct connection *c) {
	unsigned int cmd,seq;
	struct expectedreply *replyto,**lastp;
	size_t len;

	len = c->serverignore;
	if( len > c->servercount )
		len = c->servercount;

	cmd = serverCARD8(1);
	seq = serverCARD16(2);
	for( lastp = &c->expectedreplies ; 
			(replyto=*lastp) != NULL ; lastp=&replyto->next){
		if( (replyto->seq & 0xFFFFFFFF ) == seq ) {
			bool ignore = false, dontremove = false;

			assert( replyto->from != NULL);
			if( replyto->from->reply_func != NULL )
				replyto->from->reply_func(c,&ignore,&dontremove);

			if( !ignore ) {
				printf("%03d:>:0x%04x:%u: Reply to %s: ", c->id, seq, c->serverignore,replyto->from->name);
				print_parameters(c->bigendian,
					c->serverbuffer,len,replyto->from->answers,false);
				putchar('\n');
			}
			if( !dontremove ) {
				*lastp = replyto->next;
				if( replyto->next != NULL ) {
					fprintf(stderr,"%03d:>: still waiting for reply to seq=%04llx\n",c->id,replyto->next->seq);
				}
				free(replyto);
			}
			return;
		}
	}
	printf("%03d:>:%04x:%u: unexpected reply\n",
			c->id, seq, c->serverignore);
			
}

const char *errors[] = {
	"no error","Request","Value","Window",
	"Pixmap","Atom","Cursor","Font",
	"Match","Drawable","Access","Alloc",
	"Colormap","GContext","IDChoice","Name",
	"Length","Implementation"
};

static inline void print_server_error(struct connection *c) {
	unsigned int cmd = serverCARD8(1);
	printf("%03d:>:%u:Error %hhu=%s: major=%u, minor=%u, bad=%u\n",
			c->id,
			(int)serverCARD16(2),
			cmd,
			(cmd<NUM(errors))?errors[cmd]:"unknown",
			(int)serverCARD16(8),
			(int)serverCARD8(10),
			(int)serverCARD32(4));
}

void parse_client(struct connection *c) {
	size_t l;
	bool bigrequest;

	switch( c->clientstate ) {
	 case c_start:
		 if( c->clientcount < 12 ) {
			 return;
		 }
		 if( c->clientbuffer[0] == 'B' )
			 c->bigendian = true;
		 else if( c->clientbuffer[0] == 'l' )
			 c->bigendian = false;
		 else  {
			fprintf(stderr,"%03d:<: Byteorder (%d='%c') is neighter 'B' nor 'l', ignoring all further data!",c->id,(int)c->clientbuffer[0],c->clientbuffer[0]);
			c->clientstate = c_amlost;
			c->serverstate = s_amlost;
			return;
		 }
		 l = 12 + padded(clientCARD16(6)) + padded(clientCARD16(8));
		 if( c->clientcount < l ) {
			 /* wait for auth data first */
			 return;
		 }
		 c->clientignore =  l;

		 printf("%03d:<: am %s want %d:%d authorising with '%*s' of length %d\n",
				 c->id,
				 c->bigendian?"msb-first":"lsb-first",
				 (int)clientCARD16(2),
				 (int)clientCARD16(4),
				 (int)clientCARD16(6),
				 &c->clientbuffer[12],
				 (int)clientCARD16(8));
		 c->clientstate = c_normal;
		 return;
	 case c_normal:
		 if( c->clientcount < 4 ) {
			 printf("%03d:<: Warning: Waiting for rest of package (yet only got %u)!\n",c->id,c->clientcount);
			 return;
		 }
		 l = 4*clientCARD16(2);
		 if( l == 0 ) {
			 if( c->clientcount < 8 ) {
				 printf("%03d:<: Warning: Waiting for rest of package (yet only got %u)!\n",c->id,c->clientcount);
				 return;
			 }
			 l = 4*clientCARD32(4);
			 bigrequest = true;
		 } else
			 bigrequest = false;
		 if( c->clientcount == sizeof(c->clientbuffer) ) 
			 printf("%03d:<: Warning: buffer filled!\n",c->id);
		 else if( c->clientcount < l ) {
			 printf("%03d:<: Warning: Waiting for rest of package (yet got %u of %u)!\n",c->id,c->clientcount,l);
			 return;
		 }
		 c->clientignore = l;
		 print_client_request(c,bigrequest);
		 return;
	 case c_amlost:
		 c->clientignore = c->clientcount;
		 return;
	}
	assert(false);
}

void parse_server(struct connection *c) {
	/* additional len in multiple of 4 */
	unsigned int len,cmd;

	if( c->serverstate == s_amlost ) {
		c->serverignore = c->servercount;
		return;
	}
	if( c->servercount < 8 )
		return;
	len = serverCARD16(6);
	switch( c->serverstate ) {
	 case s_start:
		 if( c->servercount/4 < 2+len )
			 return;
		 c->serverstate = s_normal;
		 c->serverignore = 8+4*len;
		 cmd = serverCARD16(0);
		 switch( cmd ) {
		  case 0:
			  printf("%03d:>: Failed, version is %d:%d reason is '%*s'.\n",
					 c->id,
					 (int)serverCARD16(2),
					 (int)serverCARD16(4),
					 (int)(4*len),
					 &c->serverbuffer[8]);
		  case 2:
			  printf("%03d:>: More authentication needed, reason is '%*s'.\n",
					 c->id,
					 (int)(4*len),
					 &c->serverbuffer[8]);
		  case 1:
			  printf("%03d:>: Success, version is %d:%d-%d.\n",
					 c->id,
					 (int)serverCARD16(2),
					 (int)serverCARD16(4),
					 (int)serverCARD16(8));
		 }

		 return;
	 case s_normal:
		if( c->servercount < 32 )
			return;
		switch( c->serverbuffer[0] ) {
		 case 0: /* Error */
			 c->serverignore = 32;
			 print_server_error(c);
			 break;
		 case 1: /* Reply */
			 c->serverignore = 32 + 4*serverCARD32(4);
			 print_server_reply(c);
			 break;
		 default:
			c->serverignore = 32;
			print_server_event(c);
		}
		return;
	 case s_amlost:
		break;
	}
	assert(false);
}

struct event {
	const char *name;
	const struct parameter *parameters;
};

#include "events.inc"

static void print_event(bool bigendian,const unsigned char *buffer) {
	const struct event *event;
	u_int8_t code = getCARD8(0);

	if( (code & 0x80) != 0 ) 
		fputs("(generated) ",stdout);
	code &= 0x7F;
	if( code <= 1 || code > NUM(events) ) {
		printf("unknown code %hhu",code);
		return;
	}
	event = &events[code];
	printf("%s(%hhu) ",event->name,code);
	print_parameters(bigendian,buffer,32,event->parameters,false);
}
