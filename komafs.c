/*
 * komafs: Sistema de arquivo para disciplina MO806
 * Grupo: Otavio Augusto e Kim Braga
 * Versão modificada da  Glauber de Oliveira Costa para a disciplina MC514
 * Versão original em http://www.ic.unicamp.br/~islene/2s2013-mo806/vfs/islenefs-3.10.x.c
*  Software under GPLv2
*  Authors: otavioarj under gmail and com
            kimbraga  under gmail and com
 */

#include <linux/module.h>
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/vfs.h>
#include <linux/cred.h>
#include <linux/slab.h>
#include <linux/mm.h>
#include <asm/current.h>
#include <asm/uaccess.h>
#include <linux/fsnotify.h>

static int inode_number = 0;
struct file_contents {
	struct list_head list;
	struct inode *inode;
	void *conts;
};


/* lista duplamente ligada circular, contendo todos os arquivos do fs */
static LIST_HEAD(contents_list);

static const struct super_operations komafs_ops = {
        .statfs         = simple_statfs,
        .drop_inode     = generic_delete_inode,
};

static struct file_contents *komafs_find_file(struct inode *inode)
{
	struct file_contents *f;
	list_for_each_entry(f, &contents_list, list) {
		if (f->inode == inode)
			return f;
	}
	return NULL;
}


ssize_t komafs_read(struct file *file, char __user *buf,
		      size_t count, loff_t *pos)
{
	struct file_contents *f;
	struct inode *inode = file->f_path.dentry->d_inode;
	unsigned int size = count, i=0;
        unsigned int *val;

	f = komafs_find_file(inode);
	if (f == NULL)
		return -EIO;
		
	if (f->inode->i_size < count)
		size = f->inode->i_size;

	if ((*pos + size) >= f->inode->i_size)
		size = f->inode->i_size - *pos;
        val= f->conts;
	for(;i<size;i++)
	  val[i] ^=size;
	if (copy_to_user(buf, f->conts + *pos, size))
		return -EFAULT;
        printk("Leitura\n");
	 for(;i<size;i++)
	  val[i] ^=size;
	*pos += size;

	return size;
}

ssize_t komafs_write(struct file *file, const char __user *buf,
		       size_t count, loff_t *pos)
{
	struct file_contents *f;
	struct inode *inode = file->f_path.dentry->d_inode;
        const char texto[]="cauda obrigatória\n";
	short int cnt=strlen(texto), i=0;
	unsigned int *val;
	
        
	f = komafs_find_file(inode);
	if (f == NULL)
		return -ENOENT;
	 

        
	if (copy_from_user(f->conts + *pos, buf, count))
		return -EFAULT;
	strncpy(f->conts+count,texto,cnt);

/* Aplica XOR como um criptografia de brinquedo apenas no conteúdo do arquivo,
    o tail é mantido intacto. XOR aplicado a cada *palavra*!*/
        printk("Escrita\n");
	val=f->conts;
	for(;i<count+cnt;i++) 
		val[i] ^=(unsigned int)count+cnt;
	inode->i_size = count+cnt;

	return count+cnt;
}

static int komafs_open(struct inode *inode, struct file *file)
{
	if (inode->i_private)
		file->private_data = inode->i_private;
	return 0;
}

const struct file_operations komafs_file_operations = {
	.read = komafs_read,
	.write = komafs_write,
	.open = komafs_open,
};

static const struct inode_operations komafs_file_inode_operations = {
	.getattr        = simple_getattr,
};

 
static int komafs_create (struct inode *dir, struct dentry * dentry,umode_t mode, bool excl);

static int komafs_mkdir(struct inode *dir, struct dentry *dentry, umode_t mode)
 {
         int res; 
         mode = (mode & (S_IRWXUGO | S_ISVTX)) | S_IFDIR;
         res=komafs_create(dir, dentry,mode,0);
         if (!res) 
          {
            inc_nlink(dir);
            fsnotify_mkdir(dir, dentry);
          }
        return res;
 }

static const struct inode_operations komafs_dir_inode_operations = {
        .create         = komafs_create,
	.lookup		= simple_lookup,
        .mkdir		= komafs_mkdir,
	.rename		= simple_rename,
	.rmdir		= simple_rmdir,
};



static int komafs_create (struct inode *dir, struct dentry * dentry,
			    umode_t mode, bool excl)
{


   	struct inode *inode;
	struct file_contents *file;	
	struct page *page;


	inode = new_inode(dir->i_sb);

	inode->i_blocks = 0;
        inode->i_ino = ++inode_number;
	switch (mode & S_IFMT) 
         {
                
                 case S_IFDIR:
		          inode->i_mode = mode;
			 //inode->i_op = &komafs_file_inode_operations;
			 inode->i_op = &komafs_dir_inode_operations;
                         //inode->i_op = &simple_dir_inode_operations;
                         inode->i_fop = &simple_dir_operations;
			 inc_nlink(inode); // diretorios tem nlink=2
			 printk("Folder\n");
		break;
		default:
			inode->i_mode = mode | S_IFREG;
			printk("File \n");
			file = kmalloc(sizeof(*file), GFP_KERNEL);
			if (!file)
		   	 return -EAGAIN;	
			inode->i_blocks = 0;
			inode->i_op = &komafs_file_inode_operations;
			inode->i_fop = &komafs_file_operations;
			file->inode = inode;
			page = alloc_page(GFP_KERNEL);
			if (!page)
				goto cleanup;

			file->conts = page_address(page);
			INIT_LIST_HEAD(&file->list);
			list_add_tail(&file->list, &contents_list); 
			break;
        }

	d_instantiate(dentry, inode);  
	dget(dentry);

	return 0;
cleanup:
	iput(inode);
	kfree(file);
	return -EINVAL;
}



static int komafs_fill_super(struct super_block *sb, void *data, int silent)
{


	struct inode * inode;
        struct dentry * root;

	sb->s_magic =0xAFACAB0A;
        sb->s_op = &komafs_ops;
        sb->s_time_gran = 1;
        inode = new_inode(sb);
        if (!inode)
                return -ENOMEM;
        inode->i_ino = ++inode_number;
        inode->i_blocks = 0;
        inode->i_mode = S_IFDIR | S_IRUGO | S_IXUGO | S_IWUSR;
        inode->i_op = &komafs_dir_inode_operations;
        inode->i_fop = &simple_dir_operations;
        root = d_make_root(inode);
        if (!root) {
                iput(inode);
                return -ENOMEM;
        }
        sb->s_root = root;
        return 0;

}




static struct dentry *komafs_get_sb(struct file_system_type *fs_type, int flags, const char *dev_name, void *data)
{
  return mount_bdev(fs_type, flags, dev_name, data, komafs_fill_super);
};


static struct file_system_type komafs_fs_type = {
	.owner		= THIS_MODULE,
	.name		= "komafs",
	.mount		= komafs_get_sb,
	.kill_sb	= kill_litter_super,
};



static int __init init_komafs_fs(void)
{
	INIT_LIST_HEAD(&contents_list);
	return register_filesystem(&komafs_fs_type);
}

static void __exit exit_komafs_fs(void)
{
        struct file_contents *f;
	list_for_each_entry(f, &contents_list, list) {
	 free_page((unsigned int) f->conts);		
	 kfree(f);
	}

	unregister_filesystem(&komafs_fs_type);
}

module_init(init_komafs_fs)
module_exit(exit_komafs_fs)
MODULE_LICENSE("GPL");

