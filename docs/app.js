'use strict';

const channels = {
  lead: {
    title: 'Alta ganancia',
    description: 'Para riffs pesados y solos con sustain. Ajusta Pre Gain para encontrar el nivel de saturación y Post Gain para controlar la salida.'
  },
  crunch: {
    title: 'Saturación con cuerpo',
    description: 'Para acordes y ritmos con más empuje. Ajusta Pre Gain para encontrar el equilibrio entre definición y saturación.'
  },
  clean: {
    title: 'Sonidos limpios',
    description: 'Para explorar los matices de tu guitarra con menos saturación. Usa Bright para añadir brillo y la ecualización para ajustar el tono.'
  }
};

const buttons = document.querySelectorAll('[data-channel]');
buttons.forEach(button => {
  button.addEventListener('click', () => {
    const mode = button.dataset.channel;
    buttons.forEach(item => item.setAttribute('aria-pressed', String(item === button)));
    document.getElementById('channel-story').dataset.mode = mode;
    document.getElementById('channel-title').textContent = channels[mode].title;
    document.getElementById('channel-description').textContent = channels[mode].description;
  });
});
