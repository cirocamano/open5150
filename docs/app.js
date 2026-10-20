'use strict';

const channels = {
  lead: {
    kicker: '01 / ALTA GANANCIA',
    title: 'Un riff más.\nY otro más.',
    description: 'Ganancia, sustain y una voz hecha para riffs pesados y leads al frente. Sube el Pre y encuentra tu punto de saturación.'
  },
  crunch: {
    kicker: '02 / CRUNCH',
    title: 'El punto justo\nde ruptura.',
    description: 'Acordes con cuerpo y una saturación que invita a atacar las cuerdas. Ajusta el Pre para encontrar el equilibrio entre definición y empuje.'
  },
  clean: {
    kicker: '03 / LIMPIO',
    title: 'Deja respirar\na las cuerdas.',
    description: 'El lado limpio del amplificador. Baja la ganancia, explora los matices de tu guitarra y usa Bright para sumar brillo.'
  }
};

const buttons = document.querySelectorAll('[data-channel]');
buttons.forEach(button => {
  button.addEventListener('click', () => {
    const mode = button.dataset.channel;
    const channel = channels[mode];
    buttons.forEach(item => item.setAttribute('aria-pressed', String(item === button)));
    document.querySelector('.amp-stage').dataset.mode = mode;
    document.getElementById('channel-kicker').textContent = channel.kicker;
    document.getElementById('channel-title').textContent = channel.title;
    document.getElementById('channel-description').textContent = channel.description;
  });
});
