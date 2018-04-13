
const valueList = [];

const updateLoop = () =>
    fetch(window.location.href, { method: 'post', body: getPostBody() })
        .then(response => response.text())
        .then(parseResponseBody);

const updateInterval = setInterval(updateLoop, 100);

document.querySelector('.show').onclick = () =>
    document.querySelector('textarea').value = getPostBody();

const getPostBody = () =>
    valueList
        .map(v => `${v.label}=${v.value};`)
        .join('');

const parseResponseBody = body => {
    const valueConfigSet = body.split(';');
    valueConfigSet.pop();

    valueList.forEach(x => x.active = false);

    valueConfigSet.forEach(receiveValueConfig);

    valueList.forEach(value =>
        value.element.classList[value.active ? 'add' : 'remove']('active'));
};

const receiveValueConfig = valueConfig => {
    const labelAndValues = valueConfig.split('=');
    const label = labelAndValues[0];
    const match = valueList.filter(x => x.label === label)[0];

    if (match) {
        match.active = true;
    } else {
        pushNewValue(label, labelAndValues[1].split(':').map(parseFloat));
    }
};

const pushNewValue = (label, config) => {
    const newValue = {
        label: label,
        value: config[0],
        defaultValue: config[0],
        min: config[1],
        max: config[2],
        active: true
    };
    initElements(newValue);
    valueList.push(newValue);
};

const initElements = value => {
    const newNode = document.querySelector('#itemTemplate').content.cloneNode(true);
    newNode.querySelector('.label').innerText = value.label;

    const slider = newNode.querySelector('.slider');
    slider.min = value.min;
    slider.max = value.max;
    slider.step = (value.max - value.min) / 1000;
    slider.value = value.defaultValue;

    const text = newNode.querySelector('.text');
    text.value = value.defaultValue;

    const reset = newNode.querySelector('.reset');

    slider.oninput = () => value.value = text.value = slider.value;
    reset.onclick = () => value.value = text.value = slider.value = value.defaultValue;
    text.onchange = () => {
        const x = parseFloat(text.value);
        if (isFinite(x)) value.value = slider.value = x;
    };

    value.element = newNode.querySelector('.item');

    const list = document.querySelector('#list');
    list.insertBefore(newNode, list.childNodes[list.childNodes.length - 1]);
};
