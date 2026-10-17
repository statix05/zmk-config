/* SPDX-License-Identifier: MIT. Mocked protocol tests, NOT browser/device tests. */
'use strict';
const assert=require('node:assert/strict');
const setup=require('../pairing/setup-ui.js');
const peer={type:1,addr:Uint8Array.from([1,2,3,4,5,0xc6])};
assert.equal(setup.address(peer),'C6:05:04:03:02:01');
assert.deepEqual(setup.packet(4,peer),Uint8Array.from([1,4,0,1,1,2,3,4,5,0xc6,...Array(22).fill(0)]));
assert.throws(()=>setup.packet(4));
assert.throws(()=>setup.packet(3,{type:255,addr:peer.addr}));
assert.throws(()=>setup.packet(4,{type:1,addr:new Uint8Array(6)}));
assert.throws(()=>setup.packet(4,{type:0,addr:new Uint8Array(6).fill(255)}));
assert.throws(()=>setup.packet(0)); assert.throws(()=>setup.packet(256));
assert.throws(()=>setup.packet(1,peer));
assert.throws(()=>setup.packet(2,null,256));
const vendor={usagePage:0xff31,usage:0x74,featureReports:[{reportId:0x40}]};
const device={vendorId:0x2fe3,productId:0x0100,collections:[vendor]};
assert(setup.eligible(device));
assert(!setup.eligible({...device,productId:1}));
assert(!setup.eligible({...device,vendorId:1}));
assert(!setup.eligible({...device,collections:[{...vendor,usage:1}]}));
assert(!setup.eligible({...device,collections:[{...vendor,usagePage:1,usage:6}]}));
assert(!setup.eligible({...device,collections:[vendor,{...vendor,usagePage:1,usage:6}]}));
assert(!setup.eligible({...device,collections:[{...vendor,featureReports:[{reportId:1}]}]}));
(async()=>{
  let data=new Uint8Array(33); data.set([0x40,1,4,0,4,0,1,1,...peer.addr,214]);
  const d={async sendFeatureReport(id,body) {assert.equal(id,0x40);assert.deepEqual(body,setup.packet(4,peer));},
    async receiveFeatureReport(id) {assert.equal(id,0x40);return new DataView(data.buffer);}};
  const response=await setup.command(d,4,peer);
  assert.equal(response.state,4);assert.equal(response.rssi,-42);assert.equal(setup.address(response),setup.address(peer));
  data[0]=1; await assert.rejects(setup.command(d,4,peer));
  data[0]=0x40;data[1]=2;await assert.rejects(setup.command(d,4,peer));
  data[1]=1;data[2]=3;await assert.rejects(setup.command(d,4,peer));
  data[2]=4;data[3]=1;await assert.rejects(setup.command(d,4,peer));
  data=new Uint8Array(32);await assert.rejects(setup.command(d,4,peer));
  console.log('PASS: UI VID/PID/vendor-only filter, protected-collection rejection, exact-target encoding, wildcard rejection, mocked feature exchange and malformed response rejection (NOT browser/hardware)');
})().catch(e=>{console.error(e);process.exitCode=1;});
