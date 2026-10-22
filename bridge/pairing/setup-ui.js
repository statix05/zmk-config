/* SPDX-License-Identifier: MIT; no network, input reports, logging or Studio RPC. */
'use strict';
const BridgeSetup = (() => {
  const FILTER = Object.freeze({vendorId:0x2fe3, productId:0x0100, usagePage:0xff31, usage:0x74});
  const errors = ['OK','Неверная команда','Соединение/окно уже активно','Сканирование истекло: начните заново','Адрес не наблюдался','Сначала выберите именно этот адрес','Bond уже есть: замена запрещена (сначала UNBOND)'];
  function eligible(d) {
    return d.vendorId === FILTER.vendorId && d.productId === FILTER.productId &&
      d.collections.length === 1 && d.collections.every(c => c.usagePage === FILTER.usagePage &&
        c.usage === FILTER.usage && c.featureReports.some(r => r.reportId === 0x40));
  }
  function address(p) { return [...p.addr].reverse().map(x=>x.toString(16).padStart(2,'0')).join(':').toUpperCase(); }
  function packet(op, peer=null, index=0) {
    if (!Number.isInteger(op) || op<1 || op>6 || !Number.isInteger(index) || index<0 || index>255) throw Error('Invalid command');
    const c = new Uint8Array(32); c[0]=1; c[1]=op; c[2]=index;
    if (op===3 || op===4) {
      if (!peer || ![0,1].includes(peer.type) || peer.addr.length!==6 ||
          ![...peer.addr].every(x=>Number.isInteger(x)&&x>=0&&x<=255) ||
          [...peer.addr].every(x=>x===0) || [...peer.addr].every(x=>x===255)) throw Error('Exact address + type required');
      c[3]=peer.type; c.set(peer.addr,4);
    } else if(peer) throw Error('Unexpected target');
    return c;
  }
  async function command(device,op,peer=null,index=0) {
    await device.sendFeatureReport(0x40,packet(op,peer,index));
    const r=await device.receiveFeatureReport(0x40);
    if(r.byteLength!==33 || r.getUint8(0)!==0x40 || r.getUint8(1)!==1 || r.getUint8(2)!==op) throw Error('Несовместимый feature report');
    const status=r.getUint8(3); if(status) throw Error(errors[status] || 'Неизвестный status');
    return {state:r.getUint8(4),count:r.getUint8(6),type:r.getUint8(7),
      addr:Uint8Array.from({length:6},(_,i)=>r.getUint8(8+i)),rssi:r.getInt8(14),
      name:String.fromCharCode(...Uint8Array.from({length:18},(_,i)=>r.getUint8(15+i))).trim()};
  }
  return {FILTER,eligible,address,packet,command};
})();
if(typeof module!=='undefined') module.exports=BridgeSetup;
if(typeof document!=='undefined') {
  const $=id=>document.getElementById(id);
  let device=null, selected=null, busy=false;
  function controls() {
    $('connect').disabled=busy || !!device;
    for(const id of ['scan','refresh','status','unbond']) $(id).disabled=busy || !device;
    $('arm').disabled=busy || !device || !selected || !$('ack').checked;
    for(const b of $('peers').querySelectorAll('button')) b.disabled=busy;
  }
  async function action(fn) {
    if(busy) return; busy=true; controls();
    try { await fn(); } catch(e) { $('message').textContent=e.message; }
    finally { busy=false; controls(); }
  }
  $('connect').onclick=()=>action(async()=>{
    if(!navigator.hid || !window.isSecureContext) throw Error('Нужен WebHID / secure context Chromium');
    const ds=await navigator.hid.requestDevice({filters:[BridgeSetup.FILTER]});
    const vendorDevices=ds.filter(BridgeSetup.eligible);
    if(vendorDevices.length!==1) throw Error('Неверный/неоднозначный USB vendor интерфейс: отказ');
    await vendorDevices[0].open(); device=vendorDevices[0]; $('message').textContent='USB vendor интерфейс открыт. Pairing НЕ разрешён.';
  });
  $('scan').onclick=()=>action(async()=>{
    await BridgeSetup.command(device,1); selected=null; $('ack').checked=false;
    $('target').textContent='Цель не выбрана'; $('peers').replaceChildren();
    $('message').textContent='Сбор HID-объявлений 30 с. Нажмите «Обновить объявления», затем выберите адрес + тип.';
  });
  $('refresh').onclick=()=>action(async()=>{
    const status=await BridgeSetup.command(device,5); $('peers').replaceChildren();
    for(let i=0;i<status.count;i++) {
      const p=await BridgeSetup.command(device,2,null,i), row=document.createElement('tr');
      for(const text of [BridgeSetup.address(p),p.type===0?'public':'random',String(p.rssi),p.name]) {
        const td=document.createElement('td'); td.textContent=text; row.append(td);
      }
      const cell=document.createElement('td'), b=document.createElement('button'); b.textContent='Выбрать';
      b.onclick=()=>action(async()=>{
        await BridgeSetup.command(device,3,p); selected=p; $('ack').checked=false;
        $('target').textContent=`Цель: ${BridgeSetup.address(p)} / ${p.type===0?'public':'random'}`;
        $('message').textContent='Цель выбрана. Pairing ещё НЕ разрешён.';
      }); cell.append(b); row.append(cell); $('peers').append(row);
    }
    $('message').textContent=`Кандидатов: ${status.count} (лимит 16). State ${status.state}. Имена не удостоверяют личность.`;
  });
  $('ack').onchange=controls;
  $('arm').onclick=()=>action(async()=>{
    if(!selected || !$('ack').checked) throw Error('Требуется отдельный выбор и подтверждение');
    if(!window.confirm(`Только ${BridgeSetup.address(selected)} / ${selected.type===0?'public':'random'}? Разрешить одну попытку на 20 секунд?`)) return;
    await BridgeSetup.command(device,4,selected); selected=null; $('ack').checked=false;
    $('message').textContent='Разрешена одна попытка выбранному адресу на 20 с. Имя/адрес не дают защиты от MITM. Проверьте состояние.';
  });
  $('status').onclick=()=>action(async()=>{
    const s=await BridgeSetup.command(device,5);
    $('message').textContent=`State ${s.state}: scan=${!!(s.state&1)}, selected=${!!(s.state&2)}, armed=${!!(s.state&4)}, attempt=${!!(s.state&8)}, bond=${!!(s.state&16)}, connection=${!!(s.state&32)}. Закрытое окно само по себе НЕ доказывает успешный bond.`;
  });
  $('unbond').onclick=()=>action(async()=>{
    if(!window.confirm('Стереть bond донгла? Клавиатура не сможет передавать через этот донгл, пока вы не сопряжёте их заново (BT_SEL 3 на клавиатуре — донгл найдёт её сам).')) return;
    await BridgeSetup.command(device,6);
    $('message').textContent='Bond стёрт. Переведите клавиатуру в режим pairing (BT_SEL 3) — донгл сопряжётся автоматически.';
  });
  if(navigator.hid) navigator.hid.addEventListener('disconnect',e=>{
    if(e.device===device) { device=null; selected=null; $('ack').checked=false; controls(); $('message').textContent='USB отключён: разрешение pairing закрыто firmware.'; }
  });
  controls();
}
