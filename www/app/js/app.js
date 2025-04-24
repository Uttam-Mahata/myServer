document.addEventListener('DOMContentLoaded', function() {
    // DOM Elements
    
    const taskForm = document.getElementById('task-form');
    const taskTitle = document.getElementById('task-title');
    const taskPriority = document.getElementById('task-priority');
    const taskDate = document.getElementById('task-date');
    const tasksContainer = document.getElementById('tasks');
    const filterButtons = document.querySelectorAll('.filter-btn');
    const serverTimeElement = document.getElementById('server-time');
    
    // Set today as the default date
    const today = new Date();
    const formattedDate = today.toISOString().substr(0, 10);
    taskDate.value = formattedDate;
    
    // Update server time every second
    function updateServerTime() {
        const now = new Date();
        serverTimeElement.textContent = now.toLocaleString();
    }
    
    setInterval(updateServerTime, 1000);
    updateServerTime();
    
    // Load tasks from localStorage
    let tasks = JSON.parse(localStorage.getItem('tasks')) || [];
    
    // Render tasks
    function renderTasks(tasksToRender = tasks) {
        tasksContainer.innerHTML = '';
        
        if (tasksToRender.length === 0) {
            tasksContainer.innerHTML = '<p>No tasks found. Add a new task above!</p>';
            return;
        }
        
        tasksToRender.forEach(function(task) {
            const taskElement = document.createElement('li');
            taskElement.className = `task-item ${task.completed ? 'task-completed' : ''}`;
            taskElement.setAttribute('data-id', task.id);
            taskElement.setAttribute('data-priority', task.priority);
            
            const dueDateFormatted = new Date(task.date).toLocaleDateString();
            
            taskElement.innerHTML = `
                <div class="task-info">
                    <div class="task-title">${task.title}</div>
                    <div class="task-meta">Due: ${dueDateFormatted}</div>
                </div>
                <div class="task-actions">
                    <span class="priority-tag priority-${task.priority}">${task.priority}</span>
                    <button class="complete-btn" title="Mark as ${task.completed ? 'incomplete' : 'complete'}">
                        ${task.completed ? '↩' : '✓'}
                    </button>
                    <button class="delete-btn" title="Delete task">🗑</button>
                </div>
            `;
            
            tasksContainer.appendChild(taskElement);
        });
        
        // Add event listeners to task action buttons
        document.querySelectorAll('.delete-btn').forEach(btn => {
            btn.addEventListener('click', deleteTask);
        });
        
        document.querySelectorAll('.complete-btn').forEach(btn => {
            btn.addEventListener('click', toggleTaskCompletion);
        });
    }
    
    // Add a new task
    function addTask(e) {
        e.preventDefault();
        
        if (!taskTitle.value.trim()) {
            alert('Please enter a task title');
            return;
        }
        
        const newTask = {
            id: Date.now(),
            title: taskTitle.value.trim(),
            priority: taskPriority.value,
            date: taskDate.value,
            completed: false
        };
        
        tasks.push(newTask);
        localStorage.setItem('tasks', JSON.stringify(tasks));
        
        taskTitle.value = '';
        taskDate.value = formattedDate;
        taskPriority.value = 'medium';
        
        renderTasks();
    }
    
    // Delete a task
    function deleteTask(e) {
        const taskItem = e.target.closest('.task-item');
        const taskId = parseInt(taskItem.getAttribute('data-id'));
        
        tasks = tasks.filter(task => task.id !== taskId);
        localStorage.setItem('tasks', JSON.stringify(tasks));
        
        renderTasks();
    }
    
    // Toggle task completion status
    function toggleTaskCompletion(e) {
        const taskItem = e.target.closest('.task-item');
        const taskId = parseInt(taskItem.getAttribute('data-id'));
        
        tasks = tasks.map(task => {
            if (task.id === taskId) {
                task.completed = !task.completed;
            }
            return task;
        });
        
        localStorage.setItem('tasks', JSON.stringify(tasks));
        renderTasks();
    }
    
    // Filter tasks
    function filterTasks(e) {
        const filter = e.target.getAttribute('data-filter');
        
        // Update active button
        filterButtons.forEach(btn => btn.classList.remove('active'));
        e.target.classList.add('active');
        
        if (filter === 'all') {
            renderTasks();
            return;
        }
        
        const filteredTasks = tasks.filter(task => task.priority === filter);
        renderTasks(filteredTasks);
    }
    
    // Event Listeners
    taskForm.addEventListener('submit', addTask);
    
    filterButtons.forEach(button => {
        button.addEventListener('click', filterTasks);
    });
    
    // Initial render
    renderTasks();
});
