#ifndef _ENCRYPT_ECB_H_
#define _ENCRYPT_ECB_H_

#include "utils.h"

__global__ static void encrypt_Kernel( uint32_t* dev_input, uint32_t* dev_output, 
size_t inputSize, uint32_t* dev_sm_te1, uint32_t* dev_sm_te2, uint32_t* dev_sm_te3, uint32_t* dev_sm_te4,  uint8_t* dev_sm_sbox)
{
    // Index calculations
    int tid         = threadIdx.y*blockDim.x + threadIdx.x;     //local id
    int x           = blockIdx.x * blockDim.x + threadIdx.x;    //global x id
    int y           = blockIdx.y * blockDim.y + threadIdx.y;    //global y id
    int w           = blockDim.x * gridDim.x;                   //width of the grid
    int global_tid  = y*w + x;                                  //global id

    int blockSize = blockDim.x * blockDim.y; 
    
    uint32_t w1,w2,w3,w4,s1,s2,s3,s4;
    int ROUNDS = 11;

    // store the T-boxes and sbox in shared memory.
    __shared__ uint32_t sm_te1[256], sm_te2[256], sm_te3[256], sm_te4[256];
    __shared__ uint8_t sm_sbox[256];
    
    // Loading shared memory. 256 elements are needed
    int elemPerThread = 256/blockSize;
    
    if ( !elemPerThread && tid<256) {
        //load dev_sm_te1, dev_sm_te2, dev_sm_te3, dev_sm_te4 and
        // sm_sbox to share memory variables sm_te1, sm_te2,
        //sm_te3, sm_te4 and sm_sbox;
        sm_te1[tid]   = dev_sm_te1[tid];
        sm_te2[tid]   = dev_sm_te2[tid];
        sm_te3[tid]   = dev_sm_te3[tid];
        sm_te4[tid]   = dev_sm_te4[tid];
        sm_sbox[tid]  = dev_sm_sbox[tid];
    }
    else {
        for(int i=0; i<elemPerThread; i++) {
            sm_te1[tid*elemPerThread  + i]   = dev_sm_te1[tid*elemPerThread + i];
            sm_te2[tid*elemPerThread  + i]   = dev_sm_te2[tid*elemPerThread + i];
            sm_te3[tid*elemPerThread  + i]   = dev_sm_te3[tid*elemPerThread + i];
            sm_te4[tid*elemPerThread  + i]   = dev_sm_te4[tid*elemPerThread + i];
            sm_sbox[tid*elemPerThread + i]   = dev_sm_sbox[tid*elemPerThread + i];
        }
        int modEPT = 256%blockSize; //256 is not a multiple of blockSize
        if(!modEPT && (tid == blockSize-1)) {
            for(int i=0; i<modEPT; i++) {
                sm_te1[tid*(elemPerThread+1)  + i]   = dev_sm_te1[tid*(elemPerThread+1) + i];
                sm_te2[tid*(elemPerThread+1)  + i]   = dev_sm_te2[tid*(elemPerThread+1) + i];
                sm_te3[tid*(elemPerThread+1)  + i]   = dev_sm_te3[tid*(elemPerThread+1) + i];
                sm_te4[tid*(elemPerThread+1)  + i]   = dev_sm_te4[tid*(elemPerThread+1) + i];
                sm_sbox[tid*(elemPerThread+1) + i]   = dev_sm_sbox[tid*(elemPerThread+1) + i];
            }
        }
    }
    __syncthreads();

    int warps = inputSize/512;
    int warpID = global_tid/warpSize;
    int laneID = global_tid%warpSize;
    // Each warp treat 512 bytes. 
    if(warpID < warpSize*warps) {
        
        //load the cipher blocks, all the global memory transactions are
        //coalesced. The original plain text load from files, due to the read
        //procedure reverse the byte order of the 32-bit words, So a reverse
        //process was necessary.
        w1 = dev_input[128*warpID+laneID+warpSize*0];
        w2 = dev_input[128*warpID+laneID+warpSize*1];
        w3 = dev_input[128*warpID+laneID+warpSize*2];
        w4 = dev_input[128*warpID+laneID+warpSize*3];

        // First round AddRoundKey: ex-or with round key
        w1 ^= const_expkey[0];
        w2 ^= const_expkey[1];
        w3 ^= const_expkey[2];
        w4 ^= const_expkey[3];

        // Round transformation: a set of table lookups operations.
        #pragma unroll
        for (int i = 1; i < 10; i++) {
            s1 = (sm_te4[(w4 >> 24)] ^ sm_te3[(w3 >> 16) & 0xFF] ^ sm_te2[(w2 >> 8) & 0xFF] ^ sm_te1[w1 & 0xFF]);
            s2 = (sm_te4[(w1 >> 24)] ^ sm_te3[(w4 >> 16) & 0xFF] ^ sm_te2[(w3 >> 8) & 0xFF] ^ sm_te1[w2 & 0xFF]);
            s3 = (sm_te4[(w2 >> 24)] ^ sm_te3[(w1 >> 16) & 0xFF] ^ sm_te2[(w4 >> 8) & 0xFF] ^ sm_te1[w3 & 0xFF]);
            s4 = (sm_te4[(w3 >> 24)] ^ sm_te3[(w2 >> 16) & 0xFF] ^ sm_te2[(w1 >> 8) & 0xFF] ^ sm_te1[w4 & 0xFF]);

            w1 = s1 ^ const_expkey[i * 4];
            w2 = s2 ^ const_expkey[i * 4 + 1];
            w3 = s3 ^ const_expkey[i * 4 + 2];
            w4 = s4 ^ const_expkey[i * 4 + 3];	
        } 

        // The final round doesn’t include the MixColumns
        s1  = (uint32_t)(sm_sbox[ w1        & 0xFF]);
        s1 |= (uint32_t)(sm_sbox[(w2 >>  8) & 0xFF]) << 8;
        s1 |= (uint32_t)(sm_sbox[(w3 >> 16) & 0xFF]) << 16;
        s1 |= (uint32_t)(sm_sbox[(w4 >> 24)       ]) << 24; //SubBytes and ShiftRows
        s1 ^= const_expkey[(ROUNDS - 1) * 4]; //AddRoundKey
        dev_output[128*warpID+laneID+warpSize*0] = s1 ; //store the cipher text
        
        s2  = (uint32_t)(sm_sbox[ w2        & 0xFF]);
        s2 |= (uint32_t)(sm_sbox[(w3 >>  8) & 0xFF]) << 8;
        s2 |= (uint32_t)(sm_sbox[(w4 >> 16) & 0xFF]) << 16;
        s2 |= (uint32_t)(sm_sbox[(w1 >> 24)       ]) << 24; //SubBytes and ShiftRows
        s2 ^= const_expkey[(ROUNDS - 1) * 4 + 1]; //AddRoundKey
        dev_output[128*warpID+laneID+warpSize*1] = s2 ; //store the cipher text

        s3  = (uint32_t)(sm_sbox[ w3        & 0xFF]);
        s3 |= (uint32_t)(sm_sbox[(w4 >>  8) & 0xFF]) << 8;
        s3 |= (uint32_t)(sm_sbox[(w1 >> 16) & 0xFF]) << 16;
        s3 |= (uint32_t)(sm_sbox[(w2 >> 24)       ]) << 24; //SubBytes and ShiftRows
        s3 ^= const_expkey[(ROUNDS - 1) * 4 + 2]; //AddRoundKey
        dev_output[128*warpID+laneID+warpSize*2] = s3 ; //store the cipher text

        s4  = (uint32_t)(sm_sbox[ w4        & 0xFF]);
        s4 |= (uint32_t)(sm_sbox[(w1 >>  8) & 0xFF]) << 8;
        s4 |= (uint32_t)(sm_sbox[(w2 >> 16) & 0xFF]) << 16;
        s4 |= (uint32_t)(sm_sbox[(w3 >> 24)       ]) << 24; //SubBytes and ShiftRows
        s4 ^= const_expkey[(ROUNDS - 1) * 4 + 3]; //AddRoundKey
        dev_output[128*warpID+laneID+warpSize*3] = s4 ; //store the cipher text
    }
}

#endif //_ENCRYPT_ECB_H_