/* OLSRT - WebSocket (RFC6455) */

#include "compat.h"
#include "olsrt.h"

#include <stdlib.h>
#include <string.h>

/* ============================ SHA-1 (compact) ============================ */

typedef struct ol_sha1_ctx_s {
    uint32_t h[5];
    uint64_t total_len;
    uint8_t  buf[64];
    size_t   buf_len;
} ol_sha1_ctx_t;

static void ol_sha1_init(ol_sha1_ctx_t *c) {
    c->h[0]=0x67452301u; c->h[1]=0xEFCDAB89u; c->h[2]=0x98BADCFEu; c->h[3]=0x10325476u; c->h[4]=0xC3D2E1F0u;
    c->total_len=0; c->buf_len=0;
}
static void ol_sha1_process(ol_sha1_ctx_t *c, const uint8_t *blk) {
    uint32_t w[80];
    for (int i=0;i<16;i++) w[i]=(blk[4*i]<<24)|(blk[4*i+1]<<16)|(blk[4*i+2]<<8)|blk[4*i+3];
    for (int i=16;i<80;i++){ uint32_t t=w[i-3]^w[i-8]^w[i-14]^w[i-16]; w[i]=(t<<1)|(t>>31); }
    uint32_t a=c->h[0],b=c->h[1],d=c->h[3],e=c->h[4],g=c->h[2],f,k;
    for (int i=0;i<80;i++){
        if(i<20){ f=(b&g)|((~b)&d); k=0x5A827999u; }
        else if(i<40){ f=b^g^d; k=0x6ED9EBA1u; }
        else if(i<60){ f=(b&g)|(b&d)|(g&d); k=0x8F1BBCDCu; }
        else { f=b^g^d; k=0xCA62C1D6u; }
        uint32_t t=((a<<5)|(a>>27))+f+e+k+w[i];
        e=d; d=g; g=(b<<30)|(b>>2); b=a; a=t;
    }
    c->h[0]+=a; c->h[1]+=b; c->h[2]+=g; c->h[3]+=d; c->h[4]+=e;
}
static void ol_sha1_update(ol_sha1_ctx_t *c, const uint8_t *data, size_t len) {
    c->total_len+=len;
    while (len) {
        size_t to=64-c->buf_len; if (to>len) to=len;
        memcpy(c->buf+c->buf_len,data,to);
        c->buf_len+=to; data+=to; len-=to;
        if (c->buf_len==64) { ol_sha1_process(c,c->buf); c->buf_len=0; }
    }
}
static void ol_sha1_final(ol_sha1_ctx_t *c, uint8_t out[20]) {
    uint64_t blen=c->total_len*8;
    c->buf[c->buf_len++]=0x80;
    if(c->buf_len>56){ while(c->buf_len<64) c->buf[c->buf_len++]=0; ol_sha1_process(c,c->buf); c->buf_len=0; }
    while(c->buf_len<56) c->buf[c->buf_len++]=0;
    for(int i=7;i>=0;i--) c->buf[c->buf_len++]=(uint8_t)((blen>>(8*i))&0xFF);
    ol_sha1_process(c,c->buf);
    for(int i=0;i<5;i++){ out[4*i+0]=(uint8_t)((c->h[i]>>24)&0xFF); out[4*i+1]=(uint8_t)((c->h[i]>>16)&0xFF); out[4*i+2]=(uint8_t)((c->h[i]>>8)&0xFF); out[4*i+3]=(uint8_t)(c->h[i]&0xFF); }
}
static void ol_sha1_buf(const uint8_t *data, size_t len, uint8_t out[20]) {
    ol_sha1_ctx_t c; ol_sha1_init(&c); ol_sha1_update(&c,data,len); ol_sha1_final(&c,out);
}

/* ============================ Base64 encode ============================ */

static const char ol_b64_tbl[]="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
static int ol_b64_encode(const uint8_t *in, size_t inlen, char *out, size_t outlen){
    size_t olen=((inlen+2)/3)*4; if(outlen<olen+1) return 0; size_t i=0,o=0;
    while(i+2<inlen){
        uint32_t v=(in[i]<<16)|(in[i+1]<<8)|in[i+2];
        out[o++]=ol_b64_tbl[(v>>18)&63]; out[o++]=ol_b64_tbl[(v>>12)&63];
        out[o++]=ol_b64_tbl[(v>>6)&63];  out[o++]=ol_b64_tbl[v&63];
        i+=3;
    }
    if(i<inlen){
        uint32_t v=in[i]<<16; int pad=2;
        if(i+1<inlen){ v|=in[i+1]<<8; pad=1; }
        out[o++]=ol_b64_tbl[(v>>18)&63]; out[o++]=ol_b64_tbl[(v>>12)&63];
        out[o++]=(pad==2)?'=':ol_b64_tbl[(v>>6)&63];
        out[o++]=(pad>=1)?'=':ol_b64_tbl[v&63];
    }
    out[o]='\0'; return 1;
}

/* ============================ Helpers ============================ */

#define OL_WS_GUID "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"

enum {
    OL_WS_OP_CONT   = 0x0,
    OL_WS_OP_TEXT   = 0x1,
    OL_WS_OP_BINARY = 0x2,
    OL_WS_OP_CLOSE  = 0x8,
    OL_WS_OP_PING   = 0x9,
    OL_WS_OP_PONG   = 0xA
};

typedef enum { OL_WS_ROLE_NONE=0, OL_WS_ROLE_SERVER=1, OL_WS_ROLE_CLIENT=2 } ol_ws_role_t;
typedef enum { OL_WS_ST_INIT=0, OL_WS_ST_HANDSHAKE, OL_WS_ST_OPEN, OL_WS_ST_CLOSING, OL_WS_ST_CLOSED } ol_ws_state_t;

static int ol_http_find_header(const char *req, const char *name, char *out, size_t outsz){
    size_t namelen=strlen(name);
    const char *p=req;
    while(p && *p){
        const char *line_end=strstr(p,"\r\n");
        size_t linelen=line_end?(size_t)(line_end-p):strlen(p);
        if(linelen==0) break;
        if(linelen>namelen+1 && strncasecmp(p,name,namelen)==0 && p[namelen]==':'){
            const char *v=p+namelen+1; while(*v==' '||*v=='\t') v++;
            size_t vlen=linelen-(size_t)(v-p); if(vlen>=outsz) vlen=outsz-1;
            memcpy(out,v,vlen); out[vlen]='\0'; return 0;
        }
        p=line_end?(line_end+2):NULL;
    }
    return -1;
}

/* ============================ WS context ============================ */

typedef struct ol_ws_ctx_s {
    ol_ws_role_t   role;
    ol_ws_state_t  state;
    ol_stream_t   *st;
    ol_buf_t      *rx;          /* receive buffer */
    ol_buf_t      *hs;          /* handshake buffer (client) */
    ol_ws_msg_cb   cb;
    void          *arg;
    int            expect_frag;
    uint8_t        frag_opcode;
    char           client_key_b64[64]; /* client Sec-WebSocket-Key */
} ol_ws_ctx_t;

/* Registry (small fixed map; could be improved with hash) */
#define OL_WS_MAX_REG 256
static struct { ol_stream_t *st; ol_ws_ctx_t *ctx; } ol_ws_reg[OL_WS_MAX_REG];

static ol_ws_ctx_t* ol_ws_get(ol_stream_t *st){
    for(int i=0;i<OL_WS_MAX_REG;i++) if(ol_ws_reg[i].st==st) return ol_ws_reg[i].ctx;
    return NULL;
}
static ol_ws_ctx_t* ol_ws_ensure(ol_stream_t *st){
    ol_ws_ctx_t *c=ol_ws_get(st); if(c) return c;
    for(int i=0;i<OL_WS_MAX_REG;i++) if(!ol_ws_reg[i].st){
        c=(ol_ws_ctx_t*)calloc(1,sizeof(*c)); if(!c) return NULL;
        c->st=st; c->state=OL_WS_ST_INIT;
        c->rx=ol_buf_alloc(32*1024); c->hs=ol_buf_alloc(8*1024);
        if(!c->rx||!c->hs){ if(c->rx)ol_buf_free(c->rx); if(c->hs)ol_buf_free(c->hs); free(c); return NULL; }
        ol_ws_reg[i].st=st; ol_ws_reg[i].ctx=c; return c;
    }
    return NULL;
}
static void ol_ws_detach(ol_stream_t *st){
    for(int i=0;i<OL_WS_MAX_REG;i++) if(ol_ws_reg[i].st==st){
        ol_ws_ctx_t *c=ol_ws_reg[i].ctx;
        if(c){ if(c->rx) ol_buf_free(c->rx); if(c->hs) ol_buf_free(c->hs); free(c); }
        ol_ws_reg[i].st=NULL; ol_ws_reg[i].ctx=NULL;
        return;
    }
}

/* ============================ Masking & randomness ============================ */

static void ol_ws_unmask(uint8_t *data, size_t n, const uint8_t key[4]) {
    for (size_t i=0;i<n;i++) data[i]^=key[i&3];
}
static uint32_t ol_ws_rand32(void){
    static uint32_t s=0xA5A5A5A5u; s^=s<<13; s^=s>>17; s^=s<<5; return s;
}
static void ol_ws_gen_client_key(char *out, size_t outsz){
    uint8_t rnd[16]; for(int i=0;i<16;i++) rnd[i]=(uint8_t)(ol_ws_rand32()&0xFF);
    (void)ol_b64_encode(rnd,sizeof(rnd),out,outsz);
}

/* ============================ Frame send ============================ */

static int ol_ws_send_frame(ol_ws_ctx_t *c, uint8_t opcode, const void *data, size_t n){
    if(!c || c->state!=OL_WS_ST_OPEN) return OL_ERR_STATE;
    uint8_t header[14]; size_t h=0; header[h++]=0x80|opcode; /* FIN=1 */

    int masked=(c->role==OL_WS_ROLE_CLIENT);
    if(!masked){
        if(n<=125) header[h++]=(uint8_t)n;
        else if(n<=65535){ header[h++]=126; header[h++]=(uint8_t)((n>>8)&0xFF); header[h++]=(uint8_t)(n&0xFF); }
        else { header[h++]=127; uint64_t nn=(uint64_t)n; for(int i=7;i>=0;i--) header[h++]=(uint8_t)((nn>>(8*i))&0xFF); }
        int rc=ol_stream_write(c->st, header, h, NULL, NULL); if(rc!=OL_OK) return rc;
        if(data && n) rc=ol_stream_write(c->st, data, n, NULL, NULL);
        return rc;
    } else {
        uint8_t mask[4]; for(int i=0;i<4;i++) mask[i]=(uint8_t)((ol_ws_rand32()>>(8*i))&0xFF);
        if(n<=125) header[h++]=0x80|((uint8_t)n);
        else if(n<=65535){ header[h++]=0x80|126; header[h++]=(uint8_t)((n>>8)&0xFF); header[h++]=(uint8_t)(n&0xFF); }
        else { header[h++]=0x80|127; uint64_t nn=(uint64_t)n; for(int i=7;i>=0;i--) header[h++]=(uint8_t)((nn>>(8*i))&0xFF); }
        int rc=ol_stream_write(c->st, header, h, NULL, NULL); if(rc!=OL_OK) return rc;
        rc=ol_stream_write(c->st, mask, 4, NULL, NULL); if(rc!=OL_OK) return rc;
        if(data && n){
            uint8_t *mp=(uint8_t*)malloc(n); if(!mp) return OL_ERR_ALLOC;
            memcpy(mp,data,n); ol_ws_unmask(mp,n,mask);
            rc=ol_stream_write(c->st, mp, n, NULL, NULL);
            free(mp);
        }
        return rc;
    }
}

/* ============================ Frame receive ============================ */

static void ol_ws_on_stream_data(ol_stream_t *st, const uint8_t *data, size_t n, void *arg){
    (void)arg;
    ol_ws_ctx_t *c=ol_ws_get(st); if(!c) return;

    /* Client handshake response processing */
    if(c->state==OL_WS_ST_HANDSHAKE && c->role==OL_WS_ROLE_CLIENT){
        ol_buf_append(c->hs, data, n);
        if(c->hs->len>=4){
            uint8_t *p=c->hs->data; size_t L=c->hs->len;
            for(size_t i=3;i<L;i++){
                if(p[i-3]=='\r' && p[i-2]=='\n' && p[i-1]=='\r' && p[i]=='\n'){
                    if(L<12 || strncmp((char*)p,"HTTP/1.1 101",12)!=0){ c->state=OL_WS_ST_CLOSED; return; }
                    char concat[256]; snprintf(concat,sizeof(concat),"%s%s", c->client_key_b64, OL_WS_GUID);
                    uint8_t dig[20]; ol_sha1_buf((const uint8_t*)concat, strlen(concat), dig);
                    char expect_acc[128]; ol_b64_encode(dig,sizeof(dig),expect_acc,sizeof(expect_acc));
                    char acc[128]; if(ol_http_find_header((const char*)p,"Sec-WebSocket-Accept",acc,sizeof(acc))!=0){ c->state=OL_WS_ST_CLOSED; return; }
                    if(strncmp(acc,expect_acc,strlen(expect_acc))!=0){ c->state=OL_WS_ST_CLOSED; return; }
                    c->state=OL_WS_ST_OPEN;
                    ol_stream_read_start(st, ol_ws_on_stream_data, NULL);
                    c->hs->len=0;
                    return;
                }
            }
        }
        return;
    }

    if(c->state!=OL_WS_ST_OPEN) return;

    /* Append to rx and parse frames */
    ol_buf_append(c->rx, data, n);
    uint8_t *p=c->rx->data; size_t len=c->rx->len; size_t off=0;

    while(len-off>=2){
        uint8_t b0=p[off+0], b1=p[off+1];
        int fin=(b0&0x80)!=0; uint8_t opcode=b0&0x0F;
        int masked=(b1&0x80)!=0; uint64_t plen=(uint64_t)(b1&0x7F); size_t hdr=2;
        if(plen==126){ if(len-off<hdr+2) break; plen=(uint64_t)((p[off+2]<<8)|p[off+3]); hdr+=2; }
        else if(plen==127){ if(len-off<hdr+8) break; plen=0; for(int i=0;i<8;i++) plen=(plen<<8)|p[off+2+i]; hdr+=8; }
        uint8_t mask_key[4]={0,0,0,0};
        if(masked){ if(len-off<hdr+4) break; memcpy(mask_key,p+off+hdr,4); hdr+=4; }
        if(len-off<hdr+plen) break;

        uint8_t *payload=p+off+hdr;
        if(masked) ol_ws_unmask(payload,(size_t)plen,mask_key);

        if(!fin && opcode!=OL_WS_OP_CONT){
            c->expect_frag=1; c->frag_opcode=opcode;
        } else if(opcode==OL_WS_OP_CONT && c->expect_frag){
            if(fin && c->cb){ int is_text=(c->frag_opcode==OL_WS_OP_TEXT); c->cb(st,payload,(size_t)plen,is_text,c->arg); }
            if(fin){ c->expect_frag=0; c->frag_opcode=0; }
        } else {
            switch(opcode){
                case OL_WS_OP_TEXT:   if(c->cb) c->cb(st,payload,(size_t)plen,1,c->arg); break;
                case OL_WS_OP_BINARY: if(c->cb) c->cb(st,payload,(size_t)plen,0,c->arg); break;
                case OL_WS_OP_PING:   (void)ol_ws_send_frame(c,OL_WS_OP_PONG,payload,(size_t)plen); break;
                case OL_WS_OP_PONG:   /* optional RTT tracking */ break;
                case OL_WS_OP_CLOSE:  (void)ol_ws_send_frame(c,OL_WS_OP_CLOSE,payload,(size_t)plen); c->state=OL_WS_ST_CLOSED; break;
                default:              (void)ol_ws_send_frame(c,OL_WS_OP_CLOSE,"\x03\xe8",2); c->state=OL_WS_ST_CLOSED; break;
            }
        }
        off+=hdr+(size_t)plen;
    }

    if(off>0 && off<=c->rx->len){
        memmove(c->rx->data, c->rx->data+off, c->rx->len-off);
        c->rx->len-=off;
    }
}

/* ============================ Public API ============================ */

int ol_ws_on_message(ol_stream_t *st, ol_ws_msg_cb cb, void *arg){
    ol_ws_ctx_t *c=ol_ws_ensure(st); if(!c) return OL_ERR_ALLOC;
    c->cb=cb; c->arg=arg;
    if(c->state==OL_WS_ST_OPEN) return ol_stream_read_start(st, ol_ws_on_stream_data, NULL);
    return OL_OK;
}

int ol_ws_handshake_server(ol_stream_t *st, const char *req_headers){
    if(!st || !req_headers) return OL_ERR_STATE;
    ol_ws_ctx_t *c=ol_ws_ensure(st); if(!c) return OL_ERR_ALLOC;

    char key[128]; if(ol_http_find_header(req_headers,"Sec-WebSocket-Key",key,sizeof(key))!=0) return OL_ERR_IO;
    char concat[256]; snprintf(concat,sizeof(concat),"%s%s", key, OL_WS_GUID);
    uint8_t dig[20]; ol_sha1_buf((const uint8_t*)concat, strlen(concat), dig);
    char accept[128]; if(!ol_b64_encode(dig,sizeof(dig),accept,sizeof(accept))) return OL_ERR_IO;

    char resp[512];
    int m=snprintf(resp,sizeof(resp),
        "HTTP/1.1 101 Switching Protocols\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Accept: %s\r\n"
        "\r\n", accept);

    int rc=ol_stream_write(st, resp, (size_t)m, NULL, NULL); if(rc!=OL_OK) return rc;
    c->role=OL_WS_ROLE_SERVER; c->state=OL_WS_ST_OPEN;
    return ol_stream_read_start(st, ol_ws_on_stream_data, NULL);
}

int ol_ws_handshake_client(ol_stream_t *st, const char *host, const char *path){
    if(!st || !host || !path) return OL_ERR_STATE;
    ol_ws_ctx_t *c=ol_ws_ensure(st); if(!c) return OL_ERR_ALLOC;

    ol_ws_gen_client_key(c->client_key_b64, sizeof(c->client_key_b64));

    char req[1024];
    int m=snprintf(req,sizeof(req),
        "GET %s HTTP/1.1\r\n"
        "Host: %s\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Key: %s\r\n"
        "Sec-WebSocket-Version: 13\r\n"
        "\r\n", path, host, c->client_key_b64);

    int rc=ol_stream_write(st, req, (size_t)m, NULL, NULL); if(rc!=OL_OK) return rc;
    c->role=OL_WS_ROLE_CLIENT; c->state=OL_WS_ST_HANDSHAKE;
    return ol_stream_read_start(st, ol_ws_on_stream_data, NULL);
}

int ol_ws_send_text(ol_stream_t *st, const char *text, size_t len){
    ol_ws_ctx_t *c=ol_ws_get(st); if(!c) c=ol_ws_ensure(st); if(!c || c->state!=OL_WS_ST_OPEN) return OL_ERR_STATE;
    if (!text) { text=""; len=0; }
    return ol_ws_send_frame(c, OL_WS_OP_TEXT, text, len);
}

int ol_ws_send_binary(ol_stream_t *st, const uint8_t *data, size_t len){
    ol_ws_ctx_t *c=ol_ws_get(st); if(!c) c=ol_ws_ensure(st); if(!c || c->state!=OL_WS_ST_OPEN) return OL_ERR_STATE;
    return ol_ws_send_frame(c, OL_WS_OP_BINARY, data, len);
}

int ol_ws_ping(ol_stream_t *st, const void *data, size_t len){
    ol_ws_ctx_t *c=ol_ws_get(st); if(!c || c->state!=OL_WS_ST_OPEN) return OL_ERR_STATE;
    return ol_ws_send_frame(c, OL_WS_OP_PING, data, len);
}

int ol_ws_close(ol_stream_t *st, uint16_t code, const char *reason){
    ol_ws_ctx_t *c=ol_ws_get(st); if(!c || (c->state!=OL_WS_ST_OPEN && c->state!=OL_WS_ST_CLOSING)) return OL_ERR_STATE;
    uint8_t payload[128]; size_t n=0;
    payload[n++]=(uint8_t)((code>>8)&0xFF); payload[n++]=(uint8_t)(code&0xFF);
    if(reason){ size_t rlen=strlen(reason); if(rlen>sizeof(payload)-n) rlen=sizeof(payload)-n; memcpy(payload+n,reason,rlen); n+=rlen; }
    int rc=ol_ws_send_frame(c, OL_WS_OP_CLOSE, payload, n); c->state=OL_WS_ST_CLOSING; return rc;
}

int ol_ws_is_open(ol_stream_t *st){ ol_ws_ctx_t *c=ol_ws_get(st); return (c && c->state==OL_WS_ST_OPEN) ? 1 : 0; }
int ol_ws_is_client(ol_stream_t *st){ ol_ws_ctx_t *c=ol_ws_get(st); return (c && c->role==OL_WS_ROLE_CLIENT) ? 1 : 0; }
int ol_ws_is_server(ol_stream_t *st){ ol_ws_ctx_t *c=ol_ws_get(st); return (c && c->role==OL_WS_ROLE_SERVER) ? 1 : 0; }
